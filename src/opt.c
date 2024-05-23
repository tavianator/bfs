// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

/**
 * The expression optimizer.  Different optimization levels are supported:
 *
 * -O1: basic logical simplifications, like folding (-true -and -foo) to -foo.
 *
 * -O2: dead code elimination and data flow analysis.  struct df_domain is used
 * to record data flow facts that are true at various points of evaluation.
 * Specifically, struct df_domain records the state before an expression is
 * evaluated (opt->before), and after an expression returns true
 * (opt->after_true) or false (opt->after_false).  Additionally, opt->impure
 * records the possible state before any expression with side effects is
 * evaluated.
 *
 * -O3: expression re-ordering to reduce expected cost.  In an expression like
 * (-foo -and -bar), if both -foo and -bar are pure (no side effects), they can
 * be re-ordered to (-bar -and -foo).  This is profitable if the expected cost
 * is lower for the re-ordered expression, for example if -foo is very slow or
 * -bar is likely to return false.
 *
 * -O4/-Ofast: aggressive optimizations that may affect correctness in corner
 * cases.  The main effect is to use opt->impure to determine if any side-
 * effects are reachable at all, skipping the traversal if not.
 */

#include "prelude.h"
#include "opt.h"
#include "bftw.h"
#include "bit.h"
#include "color.h"
#include "ctx.h"
#include "diag.h"
#include "dir.h"
#include "eval.h"
#include "exec.h"
#include "expr.h"
#include "list.h"
#include "pwcache.h"
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>

static char *fake_and_arg = "-and";
static char *fake_or_arg = "-or";
static char *fake_not_arg = "-not";

/**
 * The data flow domain for predicates.
 */
enum df_pred {
	/** The bottom state (unreachable). */
	PRED_BOTTOM = 0,
	/** The predicate is known to be false. */
	PRED_FALSE = 1 << false,
	/** The predicate is known to be true. */
	PRED_TRUE = 1 << true,
	/** The top state (unknown). */
	PRED_TOP = PRED_FALSE | PRED_TRUE,
};

/** Make a predicate known. */
static void constrain_pred(enum df_pred *pred, bool value) {
	*pred &= 1 << value;
}

/** Compute the join (union) of two predicates. */
static void pred_join(enum df_pred *dest, enum df_pred src) {
	*dest |= src;
}

/**
 * Types of predicates we track.
 */
enum pred_type {
	/** -readable */
	READABLE_PRED,
	/** -writable */
	WRITABLE_PRED,
	/** -executable */
	EXECUTABLE_PRED,
	/** -acl */
	ACL_PRED,
	/** -capable */
	CAPABLE_PRED,
	/** -empty */
	EMPTY_PRED,
	/** -hidden */
	HIDDEN_PRED,
	/** -nogroup */
	NOGROUP_PRED,
	/** -nouser */
	NOUSER_PRED,
	/** -sparse */
	SPARSE_PRED,
	/** -xattr */
	XATTR_PRED,
	/** The number of pred_types. */
	PRED_TYPES,
};

/** Get the name of a predicate type. */
static const char *pred_type_name(enum pred_type type) {
	switch (type) {
	case READABLE_PRED:
		return "-readable";
	case WRITABLE_PRED:
		return "-writable";
	case EXECUTABLE_PRED:
		return "-executable";
	case ACL_PRED:
		return "-acl";
	case CAPABLE_PRED:
		return "-capable";
	case EMPTY_PRED:
		return "-empty";
	case HIDDEN_PRED:
		return "-hidden";
	case NOGROUP_PRED:
		return "-nogroup";
	case NOUSER_PRED:
		return "-nouser";
	case SPARSE_PRED:
		return "-sparse";
	case XATTR_PRED:
		return "-xattr";

	case PRED_TYPES:
		break;
	}

	bfs_bug("Unknown predicate %d", (int)type);
	return "???";
}

/**
 * A contrained integer range.
 */
struct df_range {
	/** The (inclusive) minimum value. */
	long long min;
	/** The (inclusive) maximum value. */
	long long max;
};

/** Initialize an empty range. */
static void range_init_bottom(struct df_range *range) {
	range->min = LLONG_MAX;
	range->max = LLONG_MIN;
}

/** Check if a range is empty. */
static bool range_is_bottom(const struct df_range *range) {
	return range->min > range->max;
}

/** Initialize a full range. */
static void range_init_top(struct df_range *range) {
	// All ranges we currently track are non-negative
	range->min = 0;
	range->max = LLONG_MAX;
}

/** Check for an infinite range. */
static bool range_is_top(const struct df_range *range) {
	return range->min == 0 && range->max == LLONG_MAX;
}

/** Compute the minimum of two values. */
static long long min_value(long long a, long long b) {
	if (a < b) {
		return a;
	} else {
		return b;
	}
}

/** Compute the maximum of two values. */
static long long max_value(long long a, long long b) {
	if (a > b) {
		return a;
	} else {
		return b;
	}
}

/** Constrain the minimum of a range. */
static void constrain_min(struct df_range *range, long long value) {
	range->min = max_value(range->min, value);
}

/** Contrain the maximum of a range. */
static void constrain_max(struct df_range *range, long long value) {
	range->max = min_value(range->max, value);
}

/** Remove a single value from a range. */
static void range_remove(struct df_range *range, long long value) {
	if (range->min == value) {
		if (range->min == LLONG_MAX) {
			range->max = LLONG_MIN;
		} else {
			++range->min;
		}
	}

	if (range->max == value) {
		if (range->max == LLONG_MIN) {
			range->min = LLONG_MAX;
		} else {
			--range->max;
		}
	}
}

/** Compute the union of two ranges. */
static void range_join(struct df_range *dest, const struct df_range *src) {
	dest->min = min_value(dest->min, src->min);
	dest->max = max_value(dest->max, src->max);
}

/**
 * Types of ranges we track.
 */
enum range_type {
	/** Search tree depth. */
	DEPTH_RANGE,
	/** Group ID. */
	GID_RANGE,
	/** Inode number.  */
	INUM_RANGE,
	/** Hard link count. */
	LINKS_RANGE,
	/** File size. */
	SIZE_RANGE,
	/** User ID. */
	UID_RANGE,
	/** The number of range_types. */
	RANGE_TYPES,
};

/** Get the name of a range type. */
static const char *range_type_name(enum range_type type) {
	switch (type) {
	case DEPTH_RANGE:
		return "-depth";
	case GID_RANGE:
		return "-gid";
	case INUM_RANGE:
		return "-inum";
	case LINKS_RANGE:
		return "-links";
	case SIZE_RANGE:
		return "-size";
	case UID_RANGE:
		return "-uid";

	case RANGE_TYPES:
		break;
	}

	bfs_bug("Unknown range %d", (int)type);
	return "???";
}

/**
 * The data flow analysis domain.
 */
struct df_domain {
	/** The predicates we track. */
	enum df_pred preds[PRED_TYPES];

	/** The value ranges we track. */
	struct df_range ranges[RANGE_TYPES];

	/** Bitmask of possible -types. */
	unsigned int types;
	/** Bitmask of possible -xtypes. */
	unsigned int xtypes;
};

/** Set a data flow value to bottom. */
static void df_init_bottom(struct df_domain *value) {
	for (int i = 0; i < PRED_TYPES; ++i) {
		value->preds[i] = PRED_BOTTOM;
	}

	for (int i = 0; i < RANGE_TYPES; ++i) {
		range_init_bottom(&value->ranges[i]);
	}

	value->types = 0;
	value->xtypes = 0;
}

/** Determine whether a fact set is impossible. */
static bool df_is_bottom(const struct df_domain *value) {
	for (int i = 0; i < RANGE_TYPES; ++i) {
		if (range_is_bottom(&value->ranges[i])) {
			return true;
		}
	}

	for (int i = 0; i < PRED_TYPES; ++i) {
		if (value->preds[i] == PRED_BOTTOM) {
			return true;
		}
	}

	if (!value->types || !value->xtypes) {
		return true;
	}

	return false;
}

/** Initialize some data flow value. */
static void df_init_top(struct df_domain *value) {
	for (int i = 0; i < PRED_TYPES; ++i) {
		value->preds[i] = PRED_TOP;
	}

	for (int i = 0; i < RANGE_TYPES; ++i) {
		range_init_top(&value->ranges[i]);
	}

	value->types = ~0;
	value->xtypes = ~0;
}

/** Check for the top element. */
static bool df_is_top(const struct df_domain *value) {
        for (int i = 0; i < PRED_TYPES; ++i) {
                if (value->preds[i] != PRED_TOP) {
                        return false;
                }
        }

        for (int i = 0; i < RANGE_TYPES; ++i) {
                if (!range_is_top(&value->ranges[i])) {
                        return false;
                }
        }

        if (value->types != ~0U) {
                return false;
        }

        if (value->xtypes != ~0U) {
                return false;
        }

        return true;
}

/** Compute the union of two fact sets. */
static void df_join(struct df_domain *dest, const struct df_domain *src) {
	for (int i = 0; i < PRED_TYPES; ++i) {
		pred_join(&dest->preds[i], src->preds[i]);
	}

	for (int i = 0; i < RANGE_TYPES; ++i) {
		range_join(&dest->ranges[i], &src->ranges[i]);
	}

	dest->types |= src->types;
	dest->xtypes |= src->xtypes;
}

/**
 * Optimizer state.
 */
struct bfs_opt {
	/** The context we're optimizing. */
	struct bfs_ctx *ctx;
	/** Optimization level (ctx->optlevel). */
	int level;
	/** Recursion depth. */
	int depth;

	/** Whether to produce warnings. */
	bool warn;
	/** Whether the result of this expression is ignored. */
	bool ignore_result;

	/** Data flow state before this expression is evaluated. */
	struct df_domain before;
	/** Data flow state after this expression returns true. */
	struct df_domain after_true;
	/** Data flow state after this expression returns false. */
	struct df_domain after_false;
	/** Data flow state before any side-effecting expressions are evaluated. */
	struct df_domain *impure;
};

/** Log an optimization. */
attr(printf(2, 3))
static bool opt_debug(struct bfs_opt *opt, const char *format, ...) {
	if (bfs_debug_prefix(opt->ctx, DEBUG_OPT)) {
		for (int i = 0; i < opt->depth; ++i) {
			cfprintf(opt->ctx->cerr, "│ ");
		}

		va_list args;
		va_start(args, format);
		cvfprintf(opt->ctx->cerr, format, args);
		va_end(args);
		return true;
	} else {
		return false;
	}
}

/** Log a recursive call. */
attr(printf(2, 3))
static bool opt_enter(struct bfs_opt *opt, const char *format, ...) {
	int depth = opt->depth;
	if (depth > 0) {
		--opt->depth;
	}

	bool debug = opt_debug(opt, "%s", depth > 0 ? "├─╮ " : "");
	if (debug) {
		va_list args;
		va_start(args, format);
		cvfprintf(opt->ctx->cerr, format, args);
		va_end(args);
	}

	opt->depth = depth + 1;
	return debug;
}

/** Log a recursive return. */
attr(printf(2, 3))
static bool opt_leave(struct bfs_opt *opt, const char *format, ...) {
	bool debug = false;
	int depth = opt->depth;

	if (format) {
		if (depth > 1) {
			opt->depth -= 2;
		}

		debug = opt_debug(opt, "%s", depth > 1 ? "├─╯ " : "");
		if (debug) {
			va_list args;
			va_start(args, format);
			cvfprintf(opt->ctx->cerr, format, args);
			va_end(args);
		}
	}

	opt->depth = depth - 1;
	return debug;
}

/** Log a shallow visit. */
attr(printf(2, 3))
static bool opt_visit(struct bfs_opt *opt, const char *format, ...) {
	int depth = opt->depth;
	if (depth > 0) {
		--opt->depth;
	}

	bool debug = opt_debug(opt, "%s", depth > 0 ? "├─◯ " : "");
	if (debug) {
		va_list args;
		va_start(args, format);
		cvfprintf(opt->ctx->cerr, format, args);
		va_end(args);
	}

	opt->depth = depth;
	return debug;
}

/** Log the deletion of an expression. */
attr(printf(2, 3))
static bool opt_delete(struct bfs_opt *opt, const char *format, ...) {
	int depth = opt->depth;

	if (depth > 0) {
		--opt->depth;
	}

	bool debug = opt_debug(opt, "%s", depth > 0 ? "├─✘ " : "");
	if (debug) {
		va_list args;
		va_start(args, format);
		cvfprintf(opt->ctx->cerr, format, args);
		va_end(args);
	}

	opt->depth = depth;
	return debug;
}

typedef bool dump_fn(struct bfs_opt *opt, const char *format, ...);

/** Print a df_pred. */
static void pred_dump(dump_fn *dump, struct bfs_opt *opt, const struct df_domain *value, enum pred_type type) {
	dump(opt, "${blu}%s${rs}: ", pred_type_name(type));

	FILE *file = opt->ctx->cerr->file;
	switch (value->preds[type]) {
	case PRED_BOTTOM:
		fprintf(file, "⊥\n");
		break;
	case PRED_TOP:
		fprintf(file, "⊤\n");
		break;
	case PRED_TRUE:
		fprintf(file, "true\n");
		break;
	case PRED_FALSE:
		fprintf(file, "false\n");
		break;
	}
}

/** Print a df_range. */
static void range_dump(dump_fn *dump, struct bfs_opt *opt, const struct df_domain *value, enum range_type type) {
	dump(opt, "${blu}%s${rs}: ", range_type_name(type));

	FILE *file = opt->ctx->cerr->file;
	const struct df_range *range = &value->ranges[type];
	if (range_is_bottom(range)) {
		fprintf(file, "⊥\n");
	} else if (range_is_top(range)) {
		fprintf(file, "⊤\n");
	} else if (range->min == range->max) {
		fprintf(file, "%lld\n", range->min);
	} else {
		if (range->min == LLONG_MIN) {
			fprintf(file, "(-∞, ");
		} else {
			fprintf(file, "[%lld, ", range->min);
		}
		if (range->max == LLONG_MAX) {
			fprintf(file, "∞)\n");
		} else {
			fprintf(file, "%lld]\n", range->max);
		}
	}
}

/** Print a set of types. */
static void types_dump(dump_fn *dump, struct bfs_opt *opt, const char *name, unsigned int types) {
	dump(opt, "${blu}%s${rs}: ", name);

	FILE *file = opt->ctx->cerr->file;
	if (types == 0) {
		fprintf(file, " ⊥\n");
	} else if (types == ~0U) {
		fprintf(file, " ⊤\n");
	} else if (count_ones(types) < count_ones(~types)) {
		fprintf(file, " 0x%X\n", types);
	} else {
		fprintf(file, "~0x%X\n", ~types);
	}
}

/** Calculate the number of lines of df_dump() output. */
static int df_dump_lines(const struct df_domain *value) {
	int lines = 0;

	for (int i = 0; i < PRED_TYPES; ++i) {
		lines += value->preds[i] != PRED_TOP;
	}

	for (int i = 0; i < RANGE_TYPES; ++i) {
		lines += !range_is_top(&value->ranges[i]);
	}

	lines += value->types != ~0U;
	lines += value->xtypes != ~0U;

	return lines;
}

/** Get the right debugging function for a df_dump() line. */
static dump_fn *df_dump_line(int lines, int *line) {
	++*line;

	if (lines == 1) {
		return opt_visit;
	} else if (*line == 1) {
		return opt_enter;
	} else if (*line == lines) {
		return opt_leave;
	} else {
		return opt_debug;
	}
}

/** Print a data flow value. */
static void df_dump(struct bfs_opt *opt, const char *str, const struct df_domain *value) {
	if (df_is_bottom(value)) {
		opt_debug(opt, "%s: ⊥\n", str);
		return;
	} else if (df_is_top(value)) {
		opt_debug(opt, "%s: ⊤\n", str);
		return;
	}

	if (!opt_debug(opt, "%s:\n", str)) {
		return;
	}

	int lines = df_dump_lines(value);
	int line = 0;

	for (int i = 0; i < PRED_TYPES; ++i) {
		if (value->preds[i] != PRED_TOP) {
			pred_dump(df_dump_line(lines, &line), opt, value, i);
		}
	}

	for (int i = 0; i < RANGE_TYPES; ++i) {
		if (!range_is_top(&value->ranges[i])) {
			range_dump(df_dump_line(lines, &line), opt, value, i);
		}
	}

	if (value->types != ~0U) {
		types_dump(df_dump_line(lines, &line), opt, "-type", value->types);
	}

	if (value->xtypes != ~0U) {
		types_dump(df_dump_line(lines, &line), opt, "-xtype", value->xtypes);
	}
}

/** Check if an expression is constant. */
static bool is_const(const struct bfs_expr *expr) {
	return expr->eval_fn == eval_true || expr->eval_fn == eval_false;
}

/** Warn about an expression. */
attr(printf(3, 4))
static void opt_warning(const struct bfs_opt *opt, const struct bfs_expr *expr, const char *format, ...) {
	if (!opt->warn) {
		return;
	}

	if (bfs_expr_is_parent(expr) || is_const(expr)) {
		return;
	}

	if (bfs_expr_warning(opt->ctx, expr)) {
		va_list args;
		va_start(args, format);
		bfs_vwarning(opt->ctx, format, args);
		va_end(args);
	}
}

/** Remove and return an expression's children. */
static void foster_children(struct bfs_expr *expr, struct bfs_exprs *children) {
	bfs_assert(bfs_expr_is_parent(expr));

	SLIST_INIT(children);
	SLIST_EXTEND(children, &expr->children);

	expr->persistent_fds = 0;
	expr->ephemeral_fds = 0;
	expr->pure = true;
}

/** Return an expression's only child. */
static struct bfs_expr *only_child(struct bfs_expr *expr) {
	bfs_assert(bfs_expr_is_parent(expr));
	struct bfs_expr *child = bfs_expr_children(expr);
	bfs_assert(child && !child->next);
	return child;
}

/** Foster an expression's only child. */
static struct bfs_expr *foster_only_child(struct bfs_expr *expr) {
	struct bfs_expr *child = only_child(expr);
	struct bfs_exprs children;
	foster_children(expr, &children);
	return child;
}

/** An expression visitor. */
struct visitor;

/** An expression-visiting function. */
typedef struct bfs_expr *visit_fn(struct bfs_opt *opt, struct bfs_expr *expr, const struct visitor *visitor);

/** An entry in a visitor lookup table. */
struct visitor_table {
	/** The evaluation function to match on. */
	bfs_eval_fn *eval_fn;
	/** The visitor function. */
	visit_fn *visit;
};

/** Look up a visitor in a table. */
static visit_fn *look_up_visitor(const struct bfs_expr *expr, const struct visitor_table table[]) {
	for (size_t i = 0; table[i].eval_fn; ++i) {
		if (expr->eval_fn == table[i].eval_fn) {
			return table[i].visit;
		}
	}

	return NULL;
}

struct visitor {
	/** The name of this visitor. */
	const char *name;

	/** A function to call before visiting children. */
	visit_fn *enter;
	/** The default visitor. */
	visit_fn *visit;
	/** A function to call after visiting children. */
	visit_fn *leave;

	/** A visitor lookup table. */
	const struct visitor_table *table;
};

/** Recursive visitor implementation. */
static struct bfs_expr *visit_deep(struct bfs_opt *opt, struct bfs_expr *expr, const struct visitor *visitor);

/** Visit a negation. */
static struct bfs_expr *visit_not(struct bfs_opt *opt, struct bfs_expr *expr, const struct visitor *visitor) {
	struct bfs_expr *rhs = foster_only_child(expr);

	struct bfs_opt nested = *opt;
	rhs = visit_deep(&nested, rhs, visitor);
	if (!rhs) {
		return NULL;
	}

	opt->after_true = nested.after_false;
	opt->after_false = nested.after_true;

	bfs_expr_append(expr, rhs);
	return expr;
}

/** Visit a conjunction. */
static struct bfs_expr *visit_and(struct bfs_opt *opt, struct bfs_expr *expr, const struct visitor *visitor) {
	struct bfs_exprs children;
	foster_children(expr, &children);

	// Base case (-and) == (-true)
	df_init_bottom(&opt->after_false);
	struct bfs_opt nested = *opt;

	while (!SLIST_EMPTY(&children)) {
		struct bfs_expr *child = SLIST_POP(&children);

		if (SLIST_EMPTY(&children)) {
			nested.ignore_result = opt->ignore_result;
		} else {
			nested.ignore_result = false;
		}

		child = visit_deep(&nested, child, visitor);
		if (!child) {
			return NULL;
		}

		df_join(&opt->after_false, &nested.after_false);
		nested.before = nested.after_true;

		bfs_expr_append(expr, child);
	}

	opt->after_true = nested.after_true;

	return expr;
}

/** Visit a disjunction. */
static struct bfs_expr *visit_or(struct bfs_opt *opt, struct bfs_expr *expr, const struct visitor *visitor) {
	struct bfs_exprs children;
	foster_children(expr, &children);

	// Base case (-or) == (-false)
	df_init_bottom(&opt->after_true);
	struct bfs_opt nested = *opt;

	while (!SLIST_EMPTY(&children)) {
		struct bfs_expr *child = SLIST_POP(&children);

		if (SLIST_EMPTY(&children)) {
			nested.ignore_result = opt->ignore_result;
		} else {
			nested.ignore_result = false;
		}

		child = visit_deep(&nested, child, visitor);
		if (!child) {
			return NULL;
		}

		df_join(&opt->after_true, &nested.after_true);
		nested.before = nested.after_false;

		bfs_expr_append(expr, child);
	}

	opt->after_false = nested.after_false;

	return expr;
}

/** Visit a comma expression. */
static struct bfs_expr *visit_comma(struct bfs_opt *opt, struct bfs_expr *expr, const struct visitor *visitor) {
	struct bfs_exprs children;
	foster_children(expr, &children);

	struct bfs_opt nested = *opt;

	while (!SLIST_EMPTY(&children)) {
		struct bfs_expr *child = SLIST_POP(&children);

		if (SLIST_EMPTY(&children)) {
			nested.ignore_result = opt->ignore_result;
		} else {
			nested.ignore_result = true;
		}

		child = visit_deep(&nested, child, visitor);
		if (!child) {
			return NULL;
		}

		nested.before = nested.after_true;
		df_join(&nested.before, &nested.after_false);

		bfs_expr_append(expr, child);
	}

	opt->after_true = nested.after_true;
	opt->after_false = nested.after_false;

	return expr;
}

/** Default enter() function. */
static struct bfs_expr *visit_enter(struct bfs_opt *opt, struct bfs_expr *expr, const struct visitor *visitor) {
	opt_enter(opt, "%pe\n", expr);
	opt->after_true = opt->before;
	opt->after_false = opt->before;
	return expr;
}

/** Default leave() function. */
static struct bfs_expr *visit_leave(struct bfs_opt *opt, struct bfs_expr *expr, const struct visitor *visitor) {
	opt_leave(opt, "%pe\n", expr);
	return expr;
}

static struct bfs_expr *visit_deep(struct bfs_opt *opt, struct bfs_expr *expr, const struct visitor *visitor) {
	bool entered = false;

	visit_fn *enter = visitor->enter ? visitor->enter : visit_enter;
	visit_fn *leave = visitor->leave ? visitor->leave : visit_leave;

	static const struct visitor_table table[] = {
		{eval_not, visit_not},
		{eval_and, visit_and},
		{eval_or, visit_or},
		{eval_comma, visit_comma},
		{NULL, NULL},
	};
	visit_fn *recursive = look_up_visitor(expr, table);
	if (recursive) {
		if (!entered) {
			expr = enter(opt, expr, visitor);
			if (!expr) {
				return NULL;
			}
			entered = true;
		}

		expr = recursive(opt, expr, visitor);
		if (!expr) {
			return NULL;
		}
	}

	visit_fn *general = visitor->visit;
	if (general) {
		if (!entered) {
			expr = enter(opt, expr, visitor);
			if (!expr) {
				return NULL;
			}
			entered = true;
		}

		expr = general(opt, expr, visitor);
		if (!expr) {
			return NULL;
		}
	}

	visit_fn *specific = look_up_visitor(expr, visitor->table);
	if (specific) {
		if (!entered) {
			expr = enter(opt, expr, visitor);
			if (!expr) {
				return NULL;
			}
			entered = true;
		}

		expr = specific(opt, expr, visitor);
		if (!expr) {
			return NULL;
		}
	}

	if (entered) {
		expr = leave(opt, expr, visitor);
	} else {
		opt_visit(opt, "%pe\n", expr);
	}

	return expr;
}

/** Visit an expression recursively. */
static struct bfs_expr *visit(struct bfs_opt *opt, struct bfs_expr *expr, const struct visitor *visitor) {
	opt_enter(opt, "%s()\n", visitor->name);
	expr = visit_deep(opt, expr, visitor);
	opt_leave(opt, "\n");
	return expr;
}

/** Visit an expression non-recursively. */
static struct bfs_expr *visit_shallow(struct bfs_opt *opt, struct bfs_expr *expr, const struct visitor *visitor) {
	visit_fn *general = visitor->visit;
	if (expr && general) {
		expr = general(opt, expr, visitor);
	}

	if (!expr) {
		return NULL;
	}

	visit_fn *specific = look_up_visitor(expr, visitor->table);
	if (specific) {
		expr = specific(opt, expr, visitor);
	}

	return expr;
}

/** Annotate -{execut,read,writ}able. */
static struct bfs_expr *annotate_access(struct bfs_opt *opt, struct bfs_expr *expr, const struct visitor *visitor) {
	expr->probability = 1.0;
	if (expr->num & R_OK) {
		expr->probability *= 0.99;
	}
	if (expr->num & W_OK) {
		expr->probability *= 0.8;
	}
	if (expr->num & X_OK) {
		expr->probability *= 0.2;
	}

	return expr;
}

/** Annotate -empty. */
static struct bfs_expr *annotate_empty(struct bfs_opt *opt, struct bfs_expr *expr, const struct visitor *visitor) {
	if (opt->level >= 4) {
		// Since -empty attempts to open and read directories, it may
		// have side effects such as reporting permission errors, and
		// thus shouldn't be re-ordered without aggressive optimizations
		expr->pure = true;
	}

	return expr;
}

/** Annotate -exec. */
static struct bfs_expr *annotate_exec(struct bfs_opt *opt, struct bfs_expr *expr, const struct visitor *visitor) {
	if (expr->exec->flags & BFS_EXEC_MULTI) {
		expr->always_true = true;
	} else {
		expr->cost = 1000000.0;
	}

	return expr;
}

/** Annotate -name/-lname/-path. */
static struct bfs_expr *annotate_fnmatch(struct bfs_opt *opt, struct bfs_expr *expr, const struct visitor *visitor) {
	if (expr->literal) {
		expr->probability = 0.1;
	} else {
		expr->probability = 0.5;
	}

	return expr;
}

/** Annotate -f?print. */
static struct bfs_expr *annotate_fprint(struct bfs_opt *opt, struct bfs_expr *expr, const struct visitor *visitor) {
	const struct colors *colors = expr->cfile->colors;
	expr->calls_stat = colors && colors_need_stat(colors);
	return expr;
}

/** Estimate probability for -x?type. */
static void estimate_type_probability(struct bfs_expr *expr) {
	unsigned int types = expr->num;

	expr->probability = 0.0;
	if (types & (1 << BFS_BLK)) {
		expr->probability += 0.00000721183;
	}
	if (types & (1 << BFS_CHR)) {
		expr->probability += 0.0000499855;
	}
	if (types & (1 << BFS_DIR)) {
		expr->probability += 0.114475;
	}
	if (types & (1 << BFS_DOOR)) {
		expr->probability += 0.000001;
	}
	if (types & (1 << BFS_FIFO)) {
		expr->probability += 0.00000248684;
	}
	if (types & (1 << BFS_REG)) {
		expr->probability += 0.859772;
	}
	if (types & (1 << BFS_LNK)) {
		expr->probability += 0.0256816;
	}
	if (types & (1 << BFS_SOCK)) {
		expr->probability += 0.0000116881;
	}
	if (types & (1 << BFS_WHT)) {
		expr->probability += 0.000001;
	}
}

/** Annotate -type. */
static struct bfs_expr *annotate_type(struct bfs_opt *opt, struct bfs_expr *expr, const struct visitor *visitor) {
	estimate_type_probability(expr);
	return expr;
}

/** Annotate -xtype. */
static struct bfs_expr *annotate_xtype(struct bfs_opt *opt, struct bfs_expr *expr, const struct visitor *visitor) {
	if (opt->level >= 4) {
		// Since -xtype dereferences symbolic links, it may have side
		// effects such as reporting permission errors, and thus
		// shouldn't be re-ordered without aggressive optimizations
		expr->pure = true;
	}

	estimate_type_probability(expr);
	return expr;
}

/** Annotate a negation. */
static struct bfs_expr *annotate_not(struct bfs_opt *opt, struct bfs_expr *expr, const struct visitor *visitor) {
	struct bfs_expr *rhs = only_child(expr);
	expr->pure = rhs->pure;
	expr->always_true = rhs->always_false;
	expr->always_false = rhs->always_true;
	expr->cost = rhs->cost;
	expr->probability = 1.0 - rhs->probability;
	return expr;
}

/** Annotate a conjunction. */
static struct bfs_expr *annotate_and(struct bfs_opt *opt, struct bfs_expr *expr, const struct visitor *visitor) {
	expr->pure = true;
	expr->always_true = true;
	expr->always_false = false;
	expr->cost = 0.0;
	expr->probability = 1.0;

	for_expr (child, expr) {
		expr->pure &= child->pure;
		expr->always_true &= child->always_true;
		expr->always_false |= child->always_false;
		expr->cost += expr->probability * child->cost;
		expr->probability *= child->probability;
	}

	return expr;
}

/** Annotate a disjunction. */
static struct bfs_expr *annotate_or(struct bfs_opt *opt, struct bfs_expr *expr, const struct visitor *visitor) {
	expr->pure = true;
	expr->always_true = false;
	expr->always_false = true;
	expr->cost = 0.0;

	float false_prob = 1.0;
	for_expr (child, expr) {
		expr->pure &= child->pure;
		expr->always_true |= child->always_true;
		expr->always_false &= child->always_false;
		expr->cost += false_prob * child->cost;
		false_prob *= (1.0 - child->probability);
	}
	expr->probability = 1.0 - false_prob;

	return expr;
}

/** Annotate a comma expression. */
static struct bfs_expr *annotate_comma(struct bfs_opt *opt, struct bfs_expr *expr, const struct visitor *visitor) {
	expr->pure = true;
	expr->cost = 0.0;

	for_expr (child, expr) {
		expr->pure &= child->pure;
		expr->always_true = child->always_true;
		expr->always_false = child->always_false;
		expr->cost += child->cost;
		expr->probability = child->probability;
	}

	return expr;
}

/** Annotate an arbitrary expression. */
static struct bfs_expr *annotate_visit(struct bfs_opt *opt, struct bfs_expr *expr, const struct visitor *visitor) {
	/** Table of pure expressions. */
	static bfs_eval_fn *const pure[] = {
		eval_access,
		eval_acl,
		eval_capable,
		eval_depth,
		eval_false,
		eval_flags,
		eval_fstype,
		eval_gid,
		eval_hidden,
		eval_inum,
		eval_links,
		eval_lname,
		eval_name,
		eval_newer,
		eval_nogroup,
		eval_nouser,
		eval_path,
		eval_perm,
		eval_regex,
		eval_samefile,
		eval_size,
		eval_sparse,
		eval_time,
		eval_true,
		eval_type,
		eval_uid,
		eval_used,
		eval_xattr,
		eval_xattrname,
	};

	expr->pure = false;
	for (size_t i = 0; i < countof(pure); ++i) {
		if (expr->eval_fn == pure[i]) {
			expr->pure = true;
			break;
		}
	}

	/** Table of always-true expressions. */
	static bfs_eval_fn *const always_true[] = {
		eval_fls,
		eval_fprint,
		eval_fprint0,
		eval_fprintf,
		eval_fprintx,
		eval_limit,
		eval_prune,
		eval_true,
		// Non-returning
		eval_exit,
		eval_quit,
	};

	expr->always_true = false;
	for (size_t i = 0; i < countof(always_true); ++i) {
		if (expr->eval_fn == always_true[i]) {
			expr->always_true = true;
			break;
		}
	}

	/** Table of always-false expressions. */
	static bfs_eval_fn *const always_false[] = {
		eval_false,
		// Non-returning
		eval_exit,
		eval_quit,
	};

	expr->always_false = false;
	for (size_t i = 0; i < countof(always_false); ++i) {
		if (expr->eval_fn == always_false[i]) {
			expr->always_false = true;
			break;
		}
	}

	/** Table of stat-calling primaries. */
	static bfs_eval_fn *const calls_stat[] = {
		eval_empty,
		eval_flags,
		eval_fls,
		eval_fprintf,
		eval_fstype,
		eval_gid,
		eval_inum,
		eval_links,
		eval_newer,
		eval_nogroup,
		eval_nouser,
		eval_perm,
		eval_samefile,
		eval_size,
		eval_sparse,
		eval_time,
		eval_uid,
		eval_used,
		eval_xattr,
		eval_xattrname,
	};

	expr->calls_stat = false;
	for (size_t i = 0; i < countof(calls_stat); ++i) {
		if (expr->eval_fn == calls_stat[i]) {
			expr->calls_stat = true;
			break;
		}
	}

#define FAST_COST       40.0
#define FNMATCH_COST   400.0
#define STAT_COST     1000.0
#define PRINT_COST   20000.0

	/** Table of expression costs. */
	static const struct {
		bfs_eval_fn *eval_fn;
		float cost;
	} costs[] = {
		{eval_access,    STAT_COST},
		{eval_acl,       STAT_COST},
		{eval_capable,   STAT_COST},
		{eval_empty, 2 * STAT_COST}, // readdir() is worse than stat()
		{eval_flags,     STAT_COST},
		{eval_fls,      PRINT_COST},
		{eval_fprint,   PRINT_COST},
		{eval_fprint0,  PRINT_COST},
		{eval_fprintf,  PRINT_COST},
		{eval_fprintx,  PRINT_COST},
		{eval_fstype,    STAT_COST},
		{eval_gid,       STAT_COST},
		{eval_inum,      STAT_COST},
		{eval_links,     STAT_COST},
		{eval_lname,  FNMATCH_COST},
		{eval_name,   FNMATCH_COST},
		{eval_newer,     STAT_COST},
		{eval_nogroup,   STAT_COST},
		{eval_nouser,    STAT_COST},
		{eval_path,   FNMATCH_COST},
		{eval_perm,      STAT_COST},
		{eval_samefile,  STAT_COST},
		{eval_size,      STAT_COST},
		{eval_sparse,    STAT_COST},
		{eval_time,      STAT_COST},
		{eval_uid,       STAT_COST},
		{eval_used,      STAT_COST},
		{eval_xattr,     STAT_COST},
		{eval_xattrname, STAT_COST},
	};

	expr->cost = FAST_COST;
	for (size_t i = 0; i < countof(costs); ++i) {
		if (expr->eval_fn == costs[i].eval_fn) {
			expr->cost = costs[i].cost;
			break;
		}
	}

	/** Table of expression probabilities. */
	static const struct {
		/** The evaluation function with this cost. */
		bfs_eval_fn *eval_fn;
		/** The matching probability. */
		float probability;
	} probs[] = {
		{eval_acl,       0.00002},
		{eval_capable,   0.000002},
		{eval_empty,     0.01},
		{eval_false,     0.0},
		{eval_hidden,    0.01},
		{eval_nogroup,   0.01},
		{eval_nouser,    0.01},
		{eval_samefile,  0.01},
		{eval_true,      1.0},
		{eval_xattr,     0.01},
		{eval_xattrname, 0.01},
	};

	expr->probability = 0.5;
	for (size_t i = 0; i < countof(probs); ++i) {
		if (expr->eval_fn == probs[i].eval_fn) {
			expr->probability = probs[i].probability;
			break;
		}
	}

	return expr;
}

/**
 * Annotating visitor.
 */
static const struct visitor annotate = {
	.name = "annotate",
	.visit = annotate_visit,
	.table = (const struct visitor_table[]) {
		{eval_access, annotate_access},
		{eval_empty, annotate_empty},
		{eval_exec, annotate_exec},
		{eval_fprint, annotate_fprint},
		{eval_lname, annotate_fnmatch},
		{eval_name, annotate_fnmatch},
		{eval_path, annotate_fnmatch},
		{eval_type, annotate_type},
		{eval_xtype, annotate_xtype},

		{eval_not, annotate_not},
		{eval_and, annotate_and},
		{eval_or, annotate_or},
		{eval_comma, annotate_comma},

		{NULL, NULL},
	},
};

/** Create a constant expression. */
static struct bfs_expr *opt_const(struct bfs_opt *opt, bool value) {
	static bfs_eval_fn *const fns[] = {eval_false, eval_true};
	static char *fake_args[] = {"-false", "-true"};

	struct bfs_expr *expr = bfs_expr_new(opt->ctx, fns[value], 1, &fake_args[value]);
	return visit_shallow(opt, expr, &annotate);
}

/** Negate an expression, keeping it canonical. */
static struct bfs_expr *negate_expr(struct bfs_opt *opt, struct bfs_expr *expr, char **argv) {
	if (expr->eval_fn == eval_not) {
		return only_child(expr);
	} else if (expr->eval_fn == eval_true) {
		return opt_const(opt, false);
	} else if (expr->eval_fn == eval_false) {
		return opt_const(opt, true);
	}

	struct bfs_expr *ret = bfs_expr_new(opt->ctx, eval_not, 1, argv);
	if (!ret) {
		return NULL;
	}

	bfs_expr_append(ret, expr);
	return visit_shallow(opt, ret, &annotate);
}

/** Sink negations into a conjunction/disjunction using De Morgan's laws. */
static struct bfs_expr *sink_not_andor(struct bfs_opt *opt, struct bfs_expr *expr) {
	opt_debug(opt, "De Morgan's laws\n");

	char **argv = expr->argv;
	expr = only_child(expr);
	opt_enter(opt, "%pe\n", expr);

	if (expr->eval_fn == eval_and) {
		expr->eval_fn = eval_or;
		expr->argv = &fake_or_arg;
	} else {
		bfs_assert(expr->eval_fn == eval_or);
		expr->eval_fn = eval_and;
		expr->argv = &fake_and_arg;
	}

	struct bfs_exprs children;
	foster_children(expr, &children);

	struct bfs_expr *child;
	while ((child = SLIST_POP(&children))) {
		opt_enter(opt, "%pe\n", child);

		child = negate_expr(opt, child, argv);
		if (!child) {
			return NULL;
		}

		opt_leave(opt, "%pe\n", child);
		bfs_expr_append(expr, child);
	}

	opt_leave(opt, "%pe\n", expr);
	return visit_shallow(opt, expr, &annotate);
}

/** Sink a negation into a comma expression. */
static struct bfs_expr *sink_not_comma(struct bfs_opt *opt, struct bfs_expr *expr) {
	bfs_assert(expr->eval_fn == eval_comma);

	opt_enter(opt, "%pe\n", expr);

	char **argv = expr->argv;
	expr = only_child(expr);

	struct bfs_exprs children;
	foster_children(expr, &children);

	struct bfs_expr *child;
	while ((child = SLIST_POP(&children))) {
		if (SLIST_EMPTY(&children)) {
			opt_enter(opt, "%pe\n", child);
			opt_debug(opt, "sink\n");

			child = negate_expr(opt, child, argv);
			if (!child) {
				return NULL;
			}

			opt_leave(opt, "%pe\n", child);
		} else {
			opt_visit(opt, "%pe\n", child);
		}

		bfs_expr_append(expr, child);
	}

	opt_leave(opt, "%pe\n", expr);
	return visit_shallow(opt, expr, &annotate);
}

/** Canonicalize a negation. */
static struct bfs_expr *canonicalize_not(struct bfs_opt *opt, struct bfs_expr *expr, const struct visitor *visitor) {
	struct bfs_expr *rhs = only_child(expr);

	if (rhs->eval_fn == eval_not) {
		opt_debug(opt, "double negation\n");
		rhs = only_child(expr);
		return only_child(rhs);
	} else if (rhs->eval_fn == eval_and || rhs->eval_fn == eval_or) {
		return sink_not_andor(opt, expr);
	} else if (rhs->eval_fn == eval_comma) {
		return sink_not_comma(opt, expr);
	} else if (is_const(rhs)) {
		opt_debug(opt, "constant propagation\n");
		return opt_const(opt, rhs->eval_fn == eval_false);
	} else {
		return expr;
	}
}

/** Canonicalize an associative operator. */
static struct bfs_expr *canonicalize_assoc(struct bfs_opt *opt, struct bfs_expr *expr, const struct visitor *visitor) {
	struct bfs_exprs children;
	foster_children(expr, &children);

	struct bfs_exprs flat;
	SLIST_INIT(&flat);

	struct bfs_expr *child;
	while ((child = SLIST_POP(&children))) {
		if (child->eval_fn == expr->eval_fn) {
			struct bfs_expr *head = SLIST_HEAD(&child->children);
			struct bfs_expr *tail = SLIST_TAIL(&child->children);

			if (!head) {
				opt_delete(opt, "%pe [empty]\n", child);
			} else {
				opt_enter(opt, "%pe\n", child);
				opt_debug(opt, "associativity\n");
				if (head == tail) {
					opt_leave(opt, "%pe\n", head);
				} else if (head->next == tail) {
					opt_leave(opt, "%pe %pe\n", head, tail);
				} else {
					opt_leave(opt, "%pe ... %pe\n", head, tail);
				}
			}

			SLIST_EXTEND(&flat, &child->children);
		} else {
			opt_visit(opt, "%pe\n", child);
			SLIST_APPEND(&flat, child);
		}
	}

	bfs_expr_extend(expr, &flat);

	return visit_shallow(opt, expr, &annotate);
}

/**
 * Canonicalizing visitor.
 */
static const struct visitor canonicalize = {
	.name = "canonicalize",
	.table = (const struct visitor_table[]) {
		{eval_not, canonicalize_not},
		{eval_and, canonicalize_assoc},
		{eval_or, canonicalize_assoc},
		{eval_comma, canonicalize_assoc},
		{NULL, NULL},
	},
};

/** Calculate the cost of an ordered pair of expressions. */
static float expr_cost(const struct bfs_expr *parent, const struct bfs_expr *lhs, const struct bfs_expr *rhs) {
	// https://cs.stackexchange.com/a/66921/21004
	float prob = lhs->probability;
	if (parent->eval_fn == eval_or) {
		prob = 1.0 - prob;
	}
	return lhs->cost + prob * rhs->cost;
}

/** Sort a block of expressions. */
static void sort_exprs(struct bfs_opt *opt, struct bfs_expr *parent, struct bfs_exprs *exprs) {
	if (!exprs->head || !exprs->head->next) {
		return;
	}

	struct bfs_exprs left, right;
	SLIST_INIT(&left);
	SLIST_INIT(&right);

	// Split
	for (struct bfs_expr *hare = exprs->head; hare && (hare = hare->next); hare = hare->next) {
		struct bfs_expr *tortoise = SLIST_POP(exprs);
		SLIST_APPEND(&left, tortoise);
	}
	SLIST_EXTEND(&right, exprs);

	// Recurse
	sort_exprs(opt, parent, &left);
	sort_exprs(opt, parent, &right);

	// Merge
	while (!SLIST_EMPTY(&left) && !SLIST_EMPTY(&right)) {
		struct bfs_expr *lhs = left.head;
		struct bfs_expr *rhs = right.head;

		float cost = expr_cost(parent, lhs, rhs);
		float swapped = expr_cost(parent, rhs, lhs);

		if (cost <= swapped) {
			SLIST_POP(&left);
			SLIST_APPEND(exprs, lhs);
		} else {
			opt_enter(opt, "%pe %pe [${ylw}%g${rs}]\n", lhs, rhs, cost);
			SLIST_POP(&right);
			SLIST_APPEND(exprs, rhs);
			opt_leave(opt, "%pe %pe [${ylw}%g${rs}]\n", rhs, lhs, swapped);
		}
	}
	SLIST_EXTEND(exprs, &left);
	SLIST_EXTEND(exprs, &right);
}

/** Reorder children to reduce cost. */
static struct bfs_expr *reorder_andor(struct bfs_opt *opt, struct bfs_expr *expr, const struct visitor *visitor) {
	struct bfs_exprs children;
	foster_children(expr, &children);

	// Split into blocks of consecutive pure/impure expressions, and sort
	// the pure blocks
	struct bfs_exprs pure;
	SLIST_INIT(&pure);

	struct bfs_expr *child;
	while ((child = SLIST_POP(&children))) {
		if (child->pure) {
			SLIST_APPEND(&pure, child);
		} else {
			sort_exprs(opt, expr, &pure);
			bfs_expr_extend(expr, &pure);
			bfs_expr_append(expr, child);
		}
	}
	sort_exprs(opt, expr, &pure);
	bfs_expr_extend(expr, &pure);

	return visit_shallow(opt, expr, &annotate);
}

/**
 * Reordering visitor.
 */
static const struct visitor reorder = {
	.name = "reorder",
	.table = (const struct visitor_table[]) {
		{eval_and, reorder_andor},
		{eval_or, reorder_andor},
		{NULL, NULL},
	},
};

/** Transfer function for simple predicates. */
static void data_flow_pred(struct bfs_opt *opt, enum pred_type pred, bool value) {
	constrain_pred(&opt->after_true.preds[pred], value);
	constrain_pred(&opt->after_false.preds[pred], !value);
}

/** Transfer function for icmp-style ([+-]N) expressions. */
static void data_flow_icmp(struct bfs_opt *opt, const struct bfs_expr *expr, enum range_type type) {
	struct df_range *true_range = &opt->after_true.ranges[type];
	struct df_range *false_range = &opt->after_false.ranges[type];
	long long value = expr->num;

	switch (expr->int_cmp) {
	case BFS_INT_EQUAL:
		constrain_min(true_range, value);
		constrain_max(true_range, value);
		range_remove(false_range, value);
		break;

	case BFS_INT_LESS:
		constrain_min(false_range, value);
		constrain_max(true_range, value);
		range_remove(true_range, value);
		break;

	case BFS_INT_GREATER:
		constrain_max(false_range, value);
		constrain_min(true_range, value);
		range_remove(true_range, value);
		break;
	}
}

/** Transfer function for -{execut,read,writ}able. */
static struct bfs_expr *data_flow_access(struct bfs_opt *opt, struct bfs_expr *expr, const struct visitor *visitor) {
	if (expr->num & R_OK) {
		data_flow_pred(opt, READABLE_PRED, true);
	}
	if (expr->num & W_OK) {
		data_flow_pred(opt, WRITABLE_PRED, true);
	}
	if (expr->num & X_OK) {
		data_flow_pred(opt, EXECUTABLE_PRED, true);
	}

	return expr;
}

/** Transfer function for -gid. */
static struct bfs_expr *data_flow_gid(struct bfs_opt *opt, struct bfs_expr *expr, const struct visitor *visitor) {
	struct df_range *range = &opt->after_true.ranges[GID_RANGE];
	if (range->min == range->max) {
		gid_t gid = range->min;
		bool nogroup = !bfs_getgrgid(opt->ctx->groups, gid);
		if (errno == 0) {
			data_flow_pred(opt, NOGROUP_PRED, nogroup);
		}
	}

	return expr;
}

/** Transfer function for -inum. */
static struct bfs_expr *data_flow_inum(struct bfs_opt *opt, struct bfs_expr *expr, const struct visitor *visitor) {
	struct df_range *range = &opt->after_true.ranges[INUM_RANGE];
	if (range->min == range->max) {
		expr->probability = 0.01;
	} else {
		expr->probability = 0.5;
	}

	return expr;
}

/** Transfer function for -links. */
static struct bfs_expr *data_flow_links(struct bfs_opt *opt, struct bfs_expr *expr, const struct visitor *visitor) {
	struct df_range *range = &opt->after_true.ranges[LINKS_RANGE];
	if (1 >= range->min && 1 <= range->max) {
		expr->probability = 0.99;
	} else {
		expr->probability = 0.5;
	}

	return expr;
}

/** Transfer function for -samefile. */
static struct bfs_expr *data_flow_samefile(struct bfs_opt *opt, struct bfs_expr *expr, const struct visitor *visitor) {
	struct df_range *true_range = &opt->after_true.ranges[INUM_RANGE];
	constrain_min(true_range, expr->ino);
	constrain_max(true_range, expr->ino);

	struct df_range *false_range = &opt->after_false.ranges[INUM_RANGE];
	range_remove(false_range, expr->ino);

	return expr;
}

/** Transfer function for -size. */
static struct bfs_expr *data_flow_size(struct bfs_opt *opt, struct bfs_expr *expr, const struct visitor *visitor) {
	struct df_range *range = &opt->after_true.ranges[SIZE_RANGE];
	if (range->min == range->max) {
		expr->probability = 0.01;
	} else {
		expr->probability = 0.5;
	}

	return expr;
}

/** Transfer function for -type. */
static struct bfs_expr *data_flow_type(struct bfs_opt *opt, struct bfs_expr *expr, const struct visitor *visitor) {
	opt->after_true.types &= expr->num;
	opt->after_false.types &= ~expr->num;
	return expr;
}

/** Transfer function for -uid. */
static struct bfs_expr *data_flow_uid(struct bfs_opt *opt, struct bfs_expr *expr, const struct visitor *visitor) {
	struct df_range *range = &opt->after_true.ranges[UID_RANGE];
	if (range->min == range->max) {
		uid_t uid = range->min;
		bool nouser = !bfs_getpwuid(opt->ctx->users, uid);
		if (errno == 0) {
			data_flow_pred(opt, NOUSER_PRED, nouser);
		}
	}

	return expr;
}

/** Transfer function for -xtype. */
static struct bfs_expr *data_flow_xtype(struct bfs_opt *opt, struct bfs_expr *expr, const struct visitor *visitor) {
	opt->after_true.xtypes &= expr->num;
	opt->after_false.xtypes &= ~expr->num;
	return expr;
}

/** Data flow visitor entry. */
static struct bfs_expr *data_flow_enter(struct bfs_opt *opt, struct bfs_expr *expr, const struct visitor *visitor) {
	visit_enter(opt, expr, visitor);

	df_dump(opt, "before", &opt->before);

	if (!bfs_expr_is_parent(expr) && !expr->pure) {
		df_join(opt->impure, &opt->before);
		df_dump(opt, "impure", opt->impure);
	}

	return expr;
}

/** Data flow visitor exit. */
static struct bfs_expr *data_flow_leave(struct bfs_opt *opt, struct bfs_expr *expr, const struct visitor *visitor) {
	if (expr->always_true) {
		expr->probability = 1.0;
		df_init_bottom(&opt->after_false);
	}

	if (expr->always_false) {
		expr->probability = 0.0;
		df_init_bottom(&opt->after_true);
	}

	df_dump(opt, "after true", &opt->after_true);
	df_dump(opt, "after false", &opt->after_false);

	if (df_is_bottom(&opt->after_false)) {
		if (!expr->pure) {
			expr->always_true = true;
			expr->probability = 0.0;
		} else if (expr->eval_fn != eval_true) {
			opt_warning(opt, expr, "This expression is always true.\n\n");
			opt_debug(opt, "pure, always true\n");
			expr = opt_const(opt, true);
			if (!expr) {
				return NULL;
			}
		}
	}

	if (df_is_bottom(&opt->after_true)) {
		if (!expr->pure) {
			expr->always_false = true;
			expr->probability = 0.0;
		} else if (expr->eval_fn != eval_false) {
			opt_warning(opt, expr, "This expression is always false.\n\n");
			opt_debug(opt, "pure, always false\n");
			expr = opt_const(opt, false);
			if (!expr) {
				return NULL;
			}
		}
	}

	return visit_leave(opt, expr, visitor);
}

/** Data flow visitor function. */
static struct bfs_expr *data_flow_visit(struct bfs_opt *opt, struct bfs_expr *expr, const struct visitor *visitor) {
	if (opt->ignore_result && expr->pure) {
		opt_debug(opt, "ignored result\n");
		opt_warning(opt, expr, "The result of this expression is ignored.\n\n");
		expr = opt_const(opt, false);
		if (!expr) {
			return NULL;
		}
	}

	if (df_is_bottom(&opt->before)) {
		opt_debug(opt, "unreachable\n");
		opt_warning(opt, expr, "This expression is unreachable.\n\n");
		expr = opt_const(opt, false);
		if (!expr) {
			return NULL;
		}
	}

	/** Table of simple predicates. */
	static const struct {
		bfs_eval_fn *eval_fn;
		enum pred_type pred;
	} preds[] = {
		{eval_acl,         ACL_PRED},
		{eval_capable, CAPABLE_PRED},
		{eval_empty,     EMPTY_PRED},
		{eval_hidden,   HIDDEN_PRED},
		{eval_nogroup, NOGROUP_PRED},
		{eval_nouser,   NOUSER_PRED},
		{eval_sparse,   SPARSE_PRED},
		{eval_xattr,     XATTR_PRED},
	};

	for (size_t i = 0; i < countof(preds); ++i) {
		if (preds[i].eval_fn == expr->eval_fn) {
			data_flow_pred(opt, preds[i].pred, true);
			break;
		}
	}

	/** Table of simple range comparisons. */
	static const struct {
		bfs_eval_fn *eval_fn;
		enum range_type range;
	} ranges[] = {
		{eval_depth, DEPTH_RANGE},
		{eval_gid,     GID_RANGE},
		{eval_inum,   INUM_RANGE},
		{eval_links, LINKS_RANGE},
		{eval_size,   SIZE_RANGE},
		{eval_uid,     UID_RANGE},
	};

	for (size_t i = 0; i < countof(ranges); ++i) {
		if (ranges[i].eval_fn == expr->eval_fn) {
			data_flow_icmp(opt, expr, ranges[i].range);
			break;
		}
	}

	return expr;
}

/**
 * Data flow visitor.
 */
static const struct visitor data_flow = {
	.name = "data_flow",
	.enter = data_flow_enter,
	.visit = data_flow_visit,
	.leave = data_flow_leave,
	.table = (const struct visitor_table[]) {
		{eval_access, data_flow_access},
		{eval_gid, data_flow_gid},
		{eval_inum, data_flow_inum},
		{eval_links, data_flow_links},
		{eval_samefile, data_flow_samefile},
		{eval_size, data_flow_size},
		{eval_type, data_flow_type},
		{eval_uid, data_flow_uid},
		{eval_xtype, data_flow_xtype},
		{NULL, NULL},
	},
};

/** Simplify a negation. */
static struct bfs_expr *simplify_not(struct bfs_opt *opt, struct bfs_expr *expr, const struct visitor *visitor) {
	if (opt->ignore_result) {
		opt_debug(opt, "ignored result\n");
		expr = only_child(expr);
	}

	return expr;
}

/** Lift negations out of a conjunction/disjunction using De Morgan's laws. */
static struct bfs_expr *lift_andor_not(struct bfs_opt *opt, struct bfs_expr *expr) {
	// Only lift negations if it would reduce the number of (-not) expressions
	size_t added = 0, removed = 0;
	for_expr (child, expr) {
		if (child->eval_fn == eval_not) {
			++removed;
		} else {
			++added;
		}
	}
	if (added >= removed) {
		return visit_shallow(opt, expr, &annotate);
	}

	opt_debug(opt, "De Morgan's laws\n");

	if (expr->eval_fn == eval_and) {
		expr->eval_fn = eval_or;
		expr->argv = &fake_or_arg;
	} else {
		bfs_assert(expr->eval_fn == eval_or);
		expr->eval_fn = eval_and;
		expr->argv = &fake_and_arg;
	}

	struct bfs_exprs children;
	foster_children(expr, &children);

	struct bfs_expr *child;
	while ((child = SLIST_POP(&children))) {
		opt_enter(opt, "%pe\n", child);

		child = negate_expr(opt, child, &fake_not_arg);
		if (!child) {
			return NULL;
		}

		opt_leave(opt, "%pe\n", child);
		bfs_expr_append(expr, child);
	}

	expr = visit_shallow(opt, expr, &annotate);
	return negate_expr(opt, expr, &fake_not_arg);
}

/** Get the first ignorable expression in a conjunction/disjunction. */
static struct bfs_expr *first_ignorable(struct bfs_opt *opt, struct bfs_expr *expr) {
	if (opt->level < 2 || !opt->ignore_result) {
		return NULL;
	}

	struct bfs_expr *ret = NULL;
	for_expr (child, expr) {
		if (!child->pure) {
			ret = NULL;
		} else if (!ret) {
			ret = child;
		}
	}

	return ret;
}

/** Simplify a conjunction. */
static struct bfs_expr *simplify_and(struct bfs_opt *opt, struct bfs_expr *expr, const struct visitor *visitor) {
	struct bfs_expr *ignorable = first_ignorable(opt, expr);
	bool ignore = false;

	struct bfs_exprs children;
	foster_children(expr, &children);

	while (!SLIST_EMPTY(&children)) {
		struct bfs_expr *child = SLIST_POP(&children);

		if (child == ignorable) {
			ignore = true;
		}

		if (ignore) {
			opt_delete(opt, "%pe [ignored result]\n", child);
			opt_warning(opt, child, "The result of this expression is ignored.\n\n");
			continue;
		}

		if (child->eval_fn == eval_true) {
			opt_delete(opt, "%pe [conjunction elimination]\n", child);
			continue;
		}

		opt_visit(opt, "%pe\n", child);
		bfs_expr_append(expr, child);

		if (child->always_false) {
			while ((child = SLIST_POP(&children))) {
				opt_delete(opt, "%pe [short-circuit]\n", child);
			}
		}
	}

	struct bfs_expr *child = bfs_expr_children(expr);
	if (!child) {
		opt_debug(opt, "nullary identity\n");
		return opt_const(opt, true);
	} else if (!child->next) {
		opt_debug(opt, "unary identity\n");
		return only_child(expr);
	}

	return lift_andor_not(opt, expr);
}

/** Simplify a disjunction. */
static struct bfs_expr *simplify_or(struct bfs_opt *opt, struct bfs_expr *expr, const struct visitor *visitor) {
	struct bfs_expr *ignorable = first_ignorable(opt, expr);
	bool ignore = false;

	struct bfs_exprs children;
	foster_children(expr, &children);

	while (!SLIST_EMPTY(&children)) {
		struct bfs_expr *child = SLIST_POP(&children);

		if (child == ignorable) {
			ignore = true;
		}

		if (ignore) {
			opt_delete(opt, "%pe [ignored result]\n", child);
			opt_warning(opt, child, "The result of this expression is ignored.\n\n");
			continue;
		}

		if (child->eval_fn == eval_false) {
			opt_delete(opt, "%pe [disjunctive syllogism]\n", child);
			continue;
		}

		opt_visit(opt, "%pe\n", child);
		bfs_expr_append(expr, child);

		if (child->always_true) {
			while ((child = SLIST_POP(&children))) {
				opt_delete(opt, "%pe [short-circuit]\n", child);
			}
		}
	}

	struct bfs_expr *child = bfs_expr_children(expr);
	if (!child) {
		opt_debug(opt, "nullary identity\n");
		return opt_const(opt, false);
	} else if (!child->next) {
		opt_debug(opt, "unary identity\n");
		return only_child(expr);
	}

	return lift_andor_not(opt, expr);
}

/** Simplify a comma expression. */
static struct bfs_expr *simplify_comma(struct bfs_opt *opt, struct bfs_expr *expr, const struct visitor *visitor) {
	struct bfs_exprs children;
	foster_children(expr, &children);

	while (!SLIST_EMPTY(&children)) {
		struct bfs_expr *child = SLIST_POP(&children);

		if (opt->level >= 2 && child->pure && !SLIST_EMPTY(&children)) {
			opt_delete(opt, "%pe [ignored result]\n", child);
			opt_warning(opt, child, "The result of this expression is ignored.\n\n");
			continue;
		}

		opt_visit(opt, "%pe\n", child);
		bfs_expr_append(expr, child);
	}

	struct bfs_expr *child = bfs_expr_children(expr);
	if (child && !child->next) {
		opt_debug(opt, "unary identity\n");
		return only_child(expr);
	}

	return expr;
}

/**
 * Logical simplification visitor.
 */
static const struct visitor simplify = {
	.name = "simplify",
	.table = (const struct visitor_table[]) {
		{eval_not, simplify_not},
		{eval_and, simplify_and},
		{eval_or, simplify_or},
		{eval_comma, simplify_comma},
		{NULL, NULL},
	},
};

/** Optimize an expression. */
static struct bfs_expr *optimize(struct bfs_opt *opt, struct bfs_expr *expr) {
	opt_enter(opt, "pass 0:\n");
	expr = visit(opt, expr, &annotate);
	opt_leave(opt, NULL);

	/** Table of optimization passes. */
	static const struct {
		/** Minimum optlevel for this pass. */
		int level;
		/** The visitor for this pass. */
		const struct visitor *visitor;
	} passes[] = {
		{1, &canonicalize},
		{3, &reorder},
		{2, &data_flow},
		{1, &simplify},
	};

	struct df_domain impure;

	for (int i = 0; i < 3; ++i) {
		struct bfs_opt nested = *opt;
		nested.impure = &impure;
		impure = *opt->impure;

		opt_enter(&nested, "pass %d:\n", i + 1);

		for (size_t j = 0; j < countof(passes); ++j) {
			if (opt->level < passes[j].level) {
				continue;
			}

			// Skip reordering the first time through the passes, to
			// make warnings more understandable
			if (passes[j].visitor == &reorder) {
				if (i == 0) {
					continue;
				} else {
					nested.warn = false;
				}
			}

			expr = visit(&nested, expr, passes[j].visitor);
			if (!expr) {
				return NULL;
			}
		}

		opt_leave(&nested, NULL);

		if (!bfs_expr_is_parent(expr)) {
			break;
		}
	}

	*opt->impure = impure;
	return expr;
}

/** An expression predicate. */
typedef bool expr_pred(const struct bfs_expr *expr);

/** Estimate the odds that a matching expression will be evaluated. */
static float estimate_odds(const struct bfs_expr *expr, expr_pred *pred) {
	if (pred(expr)) {
		return 1.0;
	}

	float nonmatch_odds = 1.0;
	float reached_odds = 1.0;
	for_expr (child, expr) {
		float child_odds = estimate_odds(child, pred);
		nonmatch_odds *= 1.0 - reached_odds * child_odds;

		if (expr->eval_fn == eval_and) {
			reached_odds *= child->probability;
		} else if (expr->eval_fn == eval_or) {
			reached_odds *= 1.0 - child->probability;
		}
	}

	return 1.0 - nonmatch_odds;
}

/** Whether an expression calls stat(). */
static bool calls_stat(const struct bfs_expr *expr) {
	return expr->calls_stat;
}

/** Estimate the odds of calling stat(). */
static float estimate_stat_odds(struct bfs_ctx *ctx) {
	if (ctx->unique) {
		return 1.0;
	}

	float nostat_odds = 1.0 - estimate_odds(ctx->exclude, calls_stat);

	float reached_odds = 1.0 - ctx->exclude->probability;
	float expr_odds = estimate_odds(ctx->expr, calls_stat);
	nostat_odds *= 1.0 - reached_odds * expr_odds;

	return 1.0 - nostat_odds;
}

int bfs_optimize(struct bfs_ctx *ctx) {
	bfs_ctx_dump(ctx, DEBUG_OPT);

	struct df_domain impure;
	df_init_bottom(&impure);

	struct bfs_opt opt = {
		.ctx = ctx,
		.level = ctx->optlevel,
		.depth = 0,
		.warn = ctx->warn,
		.ignore_result = false,
		.impure = &impure,
	};
	df_init_top(&opt.before);

	ctx->exclude = optimize(&opt, ctx->exclude);
	if (!ctx->exclude) {
		return -1;
	}

	// Only non-excluded files are evaluated
	opt.before = opt.after_false;
	opt.ignore_result = true;

	struct df_range *depth = &opt.before.ranges[DEPTH_RANGE];
	if (ctx->mindepth > 0) {
		constrain_min(depth, ctx->mindepth);
	}
	if (ctx->maxdepth < INT_MAX) {
		constrain_max(depth, ctx->maxdepth);
	}

	ctx->expr = optimize(&opt, ctx->expr);
	if (!ctx->expr) {
		return -1;
	}

	if (opt.level >= 2 && df_is_bottom(&impure)) {
		bfs_warning(ctx, "This command won't do anything.\n\n");
	}

	const struct df_range *impure_depth = &impure.ranges[DEPTH_RANGE];
	long long mindepth = impure_depth->min;
	long long maxdepth = impure_depth->max;

	opt_enter(&opt, "post-process:\n");

	if (opt.level >= 2 && mindepth > ctx->mindepth) {
		if (mindepth > INT_MAX) {
			mindepth = INT_MAX;
		}
		opt_enter(&opt, "${blu}-mindepth${rs} ${bld}%d${rs}\n", ctx->mindepth);
		ctx->mindepth = mindepth;
		opt_leave(&opt, "${blu}-mindepth${rs} ${bld}%d${rs}\n", ctx->mindepth);
	}

	if (opt.level >= 4 && maxdepth < ctx->maxdepth) {
		if (maxdepth < INT_MIN) {
			maxdepth = INT_MIN;
		}
		opt_enter(&opt, "${blu}-maxdepth${rs} ${bld}%d${rs}\n", ctx->maxdepth);
		ctx->maxdepth = maxdepth;
		opt_leave(&opt, "${blu}-maxdepth${rs} ${bld}%d${rs}\n", ctx->maxdepth);
	}

	if (opt.level >= 3) {
		// bfs_eval() can do lazy stat() calls, but only on one thread.
		float lazy_cost = estimate_stat_odds(ctx);
		// bftw() can do eager stat() calls in parallel
		float eager_cost = 1.0 / ctx->threads;

		if (eager_cost <= lazy_cost) {
			opt_enter(&opt, "lazy stat cost: ${ylw}%g${rs}\n", lazy_cost);
			ctx->flags |= BFTW_STAT;
			opt_leave(&opt, "eager stat cost: ${ylw}%g${rs}\n", eager_cost);
		}

	}

	opt_leave(&opt, NULL);

	return 0;
}
