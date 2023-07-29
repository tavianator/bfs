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
 * cases.  The main effect is to use impure to determine if any side-effects are
 * reachable at all, and skipping the traversal if not.
 */

#include "opt.h"
#include "color.h"
#include "config.h"
#include "ctx.h"
#include "diag.h"
#include "eval.h"
#include "exec.h"
#include "expr.h"
#include "pwcache.h"
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static char *fake_and_arg = "-a";
static char *fake_or_arg = "-o";
static char *fake_not_arg = "!";

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

/**
 * The data flow analysis domain.
 */
struct df_domain {
	/** The predicates we track. */
	enum df_pred preds[PRED_TYPES];

	/** The value ranges we track. */
	struct df_range ranges[RANGE_TYPES];

	/** Bitmask of possible file types. */
	unsigned int types;
	/** Bitmask of possible link target types. */
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
	const struct bfs_ctx *ctx;

	/** Data flow state before this expression is evaluated. */
	struct df_domain before;
	/** Data flow state after this expression returns true. */
	struct df_domain after_true;
	/** Data flow state after this expression returns false. */
	struct df_domain after_false;
	/** Data flow state before any side-effecting expressions are evaluated. */
	struct df_domain *impure;
};

/** Constrain the value of a predicate. */
static void opt_constrain_pred(struct bfs_opt *opt, enum pred_type type, bool value) {
	constrain_pred(&opt->after_true.preds[type], value);
	constrain_pred(&opt->after_false.preds[type], !value);
}

/** Log an optimization. */
attr(printf(3, 4))
static bool opt_debug(const struct bfs_opt *opt, int level, const char *format, ...) {
	bfs_assert(opt->ctx->optlevel >= level);

	if (bfs_debug(opt->ctx, DEBUG_OPT, "${cyn}-O%d${rs}: ", level)) {
		va_list args;
		va_start(args, format);
		cvfprintf(opt->ctx->cerr, format, args);
		va_end(args);
		return true;
	} else {
		return false;
	}
}

/** Warn about an expression. */
attr(printf(3, 4))
static void opt_warning(const struct bfs_opt *opt, const struct bfs_expr *expr, const char *format, ...) {
	if (bfs_expr_warning(opt->ctx, expr)) {
		va_list args;
		va_start(args, format);
		bfs_vwarning(opt->ctx, format, args);
		va_end(args);
	}
}

/** Create a constant expression. */
static struct bfs_expr *opt_const(bool value) {
	static bfs_eval_fn *fns[] = {eval_false, eval_true};
	static char *fake_args[] = {"-false", "-true"};
	return bfs_expr_new(fns[value], 1, &fake_args[value]);
}

/** Extract a child expression, freeing the outer expression. */
static struct bfs_expr *extract_child_expr(struct bfs_expr *expr, struct bfs_expr **child) {
	struct bfs_expr *ret = *child;
	*child = NULL;
	bfs_expr_free(expr);
	return ret;
}

/**
 * Negate an expression.
 */
static struct bfs_expr *negate_expr(struct bfs_expr *rhs, char **argv) {
	if (rhs->eval_fn == eval_not) {
		return extract_child_expr(rhs, &rhs->rhs);
	}

	struct bfs_expr *expr = bfs_expr_new(eval_not, 1, argv);
	if (!expr) {
		bfs_expr_free(rhs);
		return NULL;
	}

	expr->lhs = NULL;
	expr->rhs = rhs;
	return expr;
}

static struct bfs_expr *optimize_not_expr(const struct bfs_opt *opt, struct bfs_expr *expr);
static struct bfs_expr *optimize_and_expr(const struct bfs_opt *opt, struct bfs_expr *expr);
static struct bfs_expr *optimize_or_expr(const struct bfs_opt *opt, struct bfs_expr *expr);

/**
 * Apply De Morgan's laws.
 */
static struct bfs_expr *de_morgan(const struct bfs_opt *opt, struct bfs_expr *expr, char **argv) {
	bool debug = opt_debug(opt, 1, "De Morgan's laws: %pe ", expr);

	struct bfs_expr *parent = negate_expr(expr, argv);
	if (!parent) {
		return NULL;
	}

	bool has_parent = true;
	if (parent->eval_fn != eval_not) {
		expr = parent;
		has_parent = false;
	}

	bfs_assert(expr->eval_fn == eval_and || expr->eval_fn == eval_or);
	if (expr->eval_fn == eval_and) {
		expr->eval_fn = eval_or;
		expr->argv = &fake_or_arg;
	} else {
		expr->eval_fn = eval_and;
		expr->argv = &fake_and_arg;
	}

	expr->lhs = negate_expr(expr->lhs, argv);
	expr->rhs = negate_expr(expr->rhs, argv);
	if (!expr->lhs || !expr->rhs) {
		bfs_expr_free(parent);
		return NULL;
	}

	if (debug) {
		cfprintf(opt->ctx->cerr, "<==> %pe\n", parent);
	}

	if (expr->lhs->eval_fn == eval_not) {
		expr->lhs = optimize_not_expr(opt, expr->lhs);
	}
	if (expr->rhs->eval_fn == eval_not) {
		expr->rhs = optimize_not_expr(opt, expr->rhs);
	}
	if (!expr->lhs || !expr->rhs) {
		bfs_expr_free(parent);
		return NULL;
	}

	if (expr->eval_fn == eval_and) {
		expr = optimize_and_expr(opt, expr);
	} else {
		expr = optimize_or_expr(opt, expr);
	}
	if (has_parent) {
		parent->rhs = expr;
	} else {
		parent = expr;
	}
	if (!expr) {
		bfs_expr_free(parent);
		return NULL;
	}

	if (has_parent) {
		parent = optimize_not_expr(opt, parent);
	}
	return parent;
}

/** Optimize an expression recursively. */
static struct bfs_expr *optimize_expr_recursive(struct bfs_opt *opt, struct bfs_expr *expr);

/**
 * Optimize a negation.
 */
static struct bfs_expr *optimize_not_expr(const struct bfs_opt *opt, struct bfs_expr *expr) {
	bfs_assert(expr->eval_fn == eval_not);

	struct bfs_expr *rhs = expr->rhs;

	int optlevel = opt->ctx->optlevel;
	if (optlevel >= 1) {
		if (rhs->eval_fn == eval_true || rhs->eval_fn == eval_false) {
			struct bfs_expr *ret = opt_const(rhs->eval_fn == eval_false);
			opt_debug(opt, 1, "constant propagation: %pe <==> %pe\n", expr, ret);
			bfs_expr_free(expr);
			return ret;
		} else if (rhs->eval_fn == eval_not) {
			opt_debug(opt, 1, "double negation: %pe <==> %pe\n", expr, rhs->rhs);
			return extract_child_expr(expr, &rhs->rhs);
		} else if (bfs_expr_never_returns(rhs)) {
			opt_debug(opt, 1, "reachability: %pe <==> %pe\n", expr, rhs);
			return extract_child_expr(expr, &expr->rhs);
		} else if ((rhs->eval_fn == eval_and || rhs->eval_fn == eval_or)
			   && (rhs->lhs->eval_fn == eval_not || rhs->rhs->eval_fn == eval_not)) {
			return de_morgan(opt, expr, expr->argv);
		}
	}

	expr->pure = rhs->pure;
	expr->always_true = rhs->always_false;
	expr->always_false = rhs->always_true;
	expr->cost = rhs->cost;
	expr->probability = 1.0 - rhs->probability;

	return expr;
}

/** Optimize a negation recursively. */
static struct bfs_expr *optimize_not_expr_recursive(struct bfs_opt *opt, struct bfs_expr *expr) {
	struct bfs_opt rhs_state = *opt;
	expr->rhs = optimize_expr_recursive(&rhs_state, expr->rhs);
	if (!expr->rhs) {
		goto fail;
	}

	opt->after_true = rhs_state.after_false;
	opt->after_false = rhs_state.after_true;

	return optimize_not_expr(opt, expr);

fail:
	bfs_expr_free(expr);
	return NULL;
}

/** Optimize a conjunction. */
static struct bfs_expr *optimize_and_expr(const struct bfs_opt *opt, struct bfs_expr *expr) {
	bfs_assert(expr->eval_fn == eval_and);

	struct bfs_expr *lhs = expr->lhs;
	struct bfs_expr *rhs = expr->rhs;

	const struct bfs_ctx *ctx = opt->ctx;
	int optlevel = ctx->optlevel;
	if (optlevel >= 1) {
		if (lhs->eval_fn == eval_true) {
			opt_debug(opt, 1, "conjunction elimination: %pe <==> %pe\n", expr, rhs);
			return extract_child_expr(expr, &expr->rhs);
		} else if (rhs->eval_fn == eval_true) {
			opt_debug(opt, 1, "conjunction elimination: %pe <==> %pe\n", expr, lhs);
			return extract_child_expr(expr, &expr->lhs);
		} else if (lhs->always_false) {
			opt_debug(opt, 1, "short-circuit: %pe <==> %pe\n", expr, lhs);
			opt_warning(opt, expr->rhs, "This expression is unreachable.\n\n");
			return extract_child_expr(expr, &expr->lhs);
		} else if (lhs->always_true && rhs->eval_fn == eval_false) {
			bool debug = opt_debug(opt, 1, "strength reduction: %pe <==> ", expr);
			struct bfs_expr *ret = extract_child_expr(expr, &expr->lhs);
			ret = negate_expr(ret, &fake_not_arg);
			if (debug && ret) {
				cfprintf(ctx->cerr, "%pe\n", ret);
			}
			return ret;
		} else if (optlevel >= 2 && lhs->pure && rhs->eval_fn == eval_false) {
			opt_debug(opt, 2, "purity: %pe <==> %pe\n", expr, rhs);
			opt_warning(opt, expr->lhs, "The result of this expression is ignored.\n\n");
			return extract_child_expr(expr, &expr->rhs);
		} else if (lhs->eval_fn == eval_not && rhs->eval_fn == eval_not) {
			return de_morgan(opt, expr, expr->lhs->argv);
		}
	}

	expr->pure = lhs->pure && rhs->pure;
	expr->always_true = lhs->always_true && rhs->always_true;
	expr->always_false = lhs->always_false || rhs->always_false;
	expr->cost = lhs->cost + lhs->probability * rhs->cost;
	expr->probability = lhs->probability * rhs->probability;

	return expr;
}

/** Optimize a conjunction recursively. */
static struct bfs_expr *optimize_and_expr_recursive(struct bfs_opt *opt, struct bfs_expr *expr) {
	struct bfs_opt lhs_state = *opt;
	expr->lhs = optimize_expr_recursive(&lhs_state, expr->lhs);
	if (!expr->lhs) {
		goto fail;
	}

	struct bfs_opt rhs_state = *opt;
	rhs_state.before = lhs_state.after_true;
	expr->rhs = optimize_expr_recursive(&rhs_state, expr->rhs);
	if (!expr->rhs) {
		goto fail;
	}

	opt->after_true = rhs_state.after_true;
	opt->after_false = lhs_state.after_false;
	df_join(&opt->after_false, &rhs_state.after_false);

	return optimize_and_expr(opt, expr);

fail:
	bfs_expr_free(expr);
	return NULL;
}

/** Optimize a disjunction. */
static struct bfs_expr *optimize_or_expr(const struct bfs_opt *opt, struct bfs_expr *expr) {
	bfs_assert(expr->eval_fn == eval_or);

	struct bfs_expr *lhs = expr->lhs;
	struct bfs_expr *rhs = expr->rhs;

	const struct bfs_ctx *ctx = opt->ctx;
	int optlevel = ctx->optlevel;
	if (optlevel >= 1) {
		if (lhs->always_true) {
			opt_debug(opt, 1, "short-circuit: %pe <==> %pe\n", expr, lhs);
			opt_warning(opt, expr->rhs, "This expression is unreachable.\n\n");
			return extract_child_expr(expr, &expr->lhs);
		} else if (lhs->eval_fn == eval_false) {
			opt_debug(opt, 1, "disjunctive syllogism: %pe <==> %pe\n", expr, rhs);
			return extract_child_expr(expr, &expr->rhs);
		} else if (rhs->eval_fn == eval_false) {
			opt_debug(opt, 1, "disjunctive syllogism: %pe <==> %pe\n", expr, lhs);
			return extract_child_expr(expr, &expr->lhs);
		} else if (lhs->always_false && rhs->eval_fn == eval_true) {
			bool debug = opt_debug(opt, 1, "strength reduction: %pe <==> ", expr);
			struct bfs_expr *ret = extract_child_expr(expr, &expr->lhs);
			ret = negate_expr(ret, &fake_not_arg);
			if (debug && ret) {
				cfprintf(ctx->cerr, "%pe\n", ret);
			}
			return ret;
		} else if (optlevel >= 2 && lhs->pure && rhs->eval_fn == eval_true) {
			opt_debug(opt, 2, "purity: %pe <==> %pe\n", expr, rhs);
			opt_warning(opt, expr->lhs, "The result of this expression is ignored.\n\n");
			return extract_child_expr(expr, &expr->rhs);
		} else if (lhs->eval_fn == eval_not && rhs->eval_fn == eval_not) {
			return de_morgan(opt, expr, expr->lhs->argv);
		}
	}

	expr->pure = lhs->pure && rhs->pure;
	expr->always_true = lhs->always_true || rhs->always_true;
	expr->always_false = lhs->always_false && rhs->always_false;
	expr->cost = lhs->cost + (1 - lhs->probability) * rhs->cost;
	expr->probability = lhs->probability + rhs->probability - lhs->probability * rhs->probability;

	return expr;
}

/** Optimize a disjunction recursively. */
static struct bfs_expr *optimize_or_expr_recursive(struct bfs_opt *opt, struct bfs_expr *expr) {
	struct bfs_opt lhs_state = *opt;
	expr->lhs = optimize_expr_recursive(&lhs_state, expr->lhs);
	if (!expr->lhs) {
		goto fail;
	}

	struct bfs_opt rhs_state = *opt;
	rhs_state.before = lhs_state.after_false;
	expr->rhs = optimize_expr_recursive(&rhs_state, expr->rhs);
	if (!expr->rhs) {
		goto fail;
	}

	opt->after_false = rhs_state.after_false;
	opt->after_true = lhs_state.after_true;
	df_join(&opt->after_true, &rhs_state.after_true);

	return optimize_or_expr(opt, expr);

fail:
	bfs_expr_free(expr);
	return NULL;
}

/** Optimize an expression in an ignored-result context. */
static struct bfs_expr *ignore_result(const struct bfs_opt *opt, struct bfs_expr *expr) {
	int optlevel = opt->ctx->optlevel;

	if (optlevel >= 1) {
		while (true) {
			if (expr->eval_fn == eval_not) {
				opt_debug(opt, 1, "ignored result: %pe --> %pe\n", expr, expr->rhs);
				opt_warning(opt, expr, "The result of this expression is ignored.\n\n");
				expr = extract_child_expr(expr, &expr->rhs);
			} else if (optlevel >= 2
			           && (expr->eval_fn == eval_and || expr->eval_fn == eval_or || expr->eval_fn == eval_comma)
			           && expr->rhs->pure) {
				opt_debug(opt, 2, "ignored result: %pe --> %pe\n", expr, expr->lhs);
				opt_warning(opt, expr->rhs, "The result of this expression is ignored.\n\n");
				expr = extract_child_expr(expr, &expr->lhs);
			} else {
				break;
			}
		}

		if (optlevel >= 2 && expr->pure && expr->eval_fn != eval_false) {
			struct bfs_expr *ret = opt_const(false);
			opt_debug(opt, 2, "ignored result: %pe --> %pe\n", expr, ret);
			opt_warning(opt, expr, "The result of this expression is ignored.\n\n");
			bfs_expr_free(expr);
			return ret;
		}
	}

	return expr;
}

/** Optimize a comma expression. */
static struct bfs_expr *optimize_comma_expr(const struct bfs_opt *opt, struct bfs_expr *expr) {
	bfs_assert(expr->eval_fn == eval_comma);

	struct bfs_expr *lhs = expr->lhs;
	struct bfs_expr *rhs = expr->rhs;

	int optlevel = opt->ctx->optlevel;
	if (optlevel >= 1) {
		lhs = expr->lhs = ignore_result(opt, lhs);

		if (bfs_expr_never_returns(lhs)) {
			opt_debug(opt, 1, "reachability: %pe <==> %pe\n", expr, lhs);
			opt_warning(opt, expr->rhs, "This expression is unreachable.\n\n");
			return extract_child_expr(expr, &expr->lhs);
		} else if ((lhs->always_true && rhs->eval_fn == eval_true)
			   || (lhs->always_false && rhs->eval_fn == eval_false)) {
			opt_debug(opt, 1, "redundancy elimination: %pe <==> %pe\n", expr, lhs);
			return extract_child_expr(expr, &expr->lhs);
		} else if (optlevel >= 2 && lhs->pure) {
			opt_debug(opt, 2, "purity: %pe <==> %pe\n", expr, rhs);
			opt_warning(opt, expr->lhs, "The result of this expression is ignored.\n\n");
			return extract_child_expr(expr, &expr->rhs);
		}
	}

	expr->pure = lhs->pure && rhs->pure;
	expr->always_true = bfs_expr_never_returns(lhs) || rhs->always_true;
	expr->always_false = bfs_expr_never_returns(lhs) || rhs->always_false;
	expr->cost = lhs->cost + rhs->cost;
	expr->probability = rhs->probability;

	return expr;
}

/** Optimize a comma expression recursively. */
static struct bfs_expr *optimize_comma_expr_recursive(struct bfs_opt *opt, struct bfs_expr *expr) {
	struct bfs_opt lhs_state = *opt;
	expr->lhs = optimize_expr_recursive(&lhs_state, expr->lhs);
	if (!expr->lhs) {
		goto fail;
	}

	struct bfs_opt rhs_state = *opt;
	rhs_state.before = lhs_state.after_true;
	df_join(&rhs_state.before, &lhs_state.after_false);

	expr->rhs = optimize_expr_recursive(&rhs_state, expr->rhs);
	if (!expr->rhs) {
		goto fail;
	}

	return optimize_comma_expr(opt, expr);

fail:
	bfs_expr_free(expr);
	return NULL;
}

/** Optimize an icmp-style ([+-]N) expression. */
static void optimize_icmp(struct bfs_opt *opt, const struct bfs_expr *expr, enum range_type type) {
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

/** Optimize -{execut,read,writ}able. */
static struct bfs_expr *optimize_access(struct bfs_opt *opt, struct bfs_expr *expr) {
	expr->probability = 1.0;

	if (expr->num & R_OK) {
		opt_constrain_pred(opt, READABLE_PRED, true);
		expr->probability *= 0.99;
	}

	if (expr->num & W_OK) {
		opt_constrain_pred(opt, WRITABLE_PRED, true);
		expr->probability *= 0.8;
	}

	if (expr->num & X_OK) {
		opt_constrain_pred(opt, EXECUTABLE_PRED, true);
		expr->probability *= 0.2;
	}

	return expr;
}

/** Optimize -empty. */
static struct bfs_expr *optimize_empty(struct bfs_opt *opt, struct bfs_expr *expr) {
	if (opt->ctx->optlevel >= 4) {
		// Since -empty attempts to open and read directories, it may
		// have side effects such as reporting permission errors, and
		// thus shouldn't be re-ordered without aggressive optimizations
		expr->pure = true;
	}

	return expr;
}

/** Optimize -{exec,ok}{,dir}. */
static struct bfs_expr *optimize_exec(struct bfs_opt *opt, struct bfs_expr *expr) {
	if (expr->exec->flags & BFS_EXEC_MULTI) {
		expr->always_true = true;
	} else {
		expr->cost = 1000000.0;
	}

	return expr;
}

/** Optimize -name/-lname/-path. */
static struct bfs_expr *optimize_fnmatch(struct bfs_opt *opt, struct bfs_expr *expr) {
	if (strchr(expr->argv[1], '*')) {
		expr->probability = 0.5;
	} else {
		expr->probability = 0.1;
	}

	return expr;
}

/** Optimize -gid. */
static struct bfs_expr *optimize_gid(struct bfs_opt *opt, struct bfs_expr *expr) {
	struct df_range *range = &opt->after_true.ranges[GID_RANGE];
	if (range->min == range->max) {
		gid_t gid = range->min;
		bool nogroup = !bfs_getgrgid(opt->ctx->groups, gid);
		if (errno == 0) {
			opt_constrain_pred(opt, NOGROUP_PRED, nogroup);
		}
	}

	return expr;
}

/** Optimize -inum. */
static struct bfs_expr *optimize_inum(struct bfs_opt *opt, struct bfs_expr *expr) {
	struct df_range *range = &opt->after_true.ranges[INUM_RANGE];
	if (range->min == range->max) {
		expr->probability = 0.01;
	} else {
		expr->probability = 0.5;
	}

	return expr;
}

/** Optimize -links. */
static struct bfs_expr *optimize_links(struct bfs_opt *opt, struct bfs_expr *expr) {
	struct df_range *range = &opt->after_true.ranges[LINKS_RANGE];
	if (1 >= range->min && 1 <= range->max) {
		expr->probability = 0.99;
	} else {
		expr->probability = 0.5;
	}

	return expr;
}

/** Optimize -uid. */
static struct bfs_expr *optimize_uid(struct bfs_opt *opt, struct bfs_expr *expr) {
	struct df_range *range = &opt->after_true.ranges[UID_RANGE];
	if (range->min == range->max) {
		uid_t uid = range->min;
		bool nouser = !bfs_getpwuid(opt->ctx->users, uid);
		if (errno == 0) {
			opt_constrain_pred(opt, NOUSER_PRED, nouser);
		}
	}

	return expr;
}

/** Optimize -samefile. */
static struct bfs_expr *optimize_samefile(struct bfs_opt *opt, struct bfs_expr *expr) {
	struct df_range *range = &opt->after_true.ranges[INUM_RANGE];
	constrain_min(range, expr->ino);
	constrain_max(range, expr->ino);
	return expr;
}

/** Optimize -size. */
static struct bfs_expr *optimize_size(struct bfs_opt *opt, struct bfs_expr *expr) {
	struct df_range *range = &opt->after_true.ranges[SIZE_RANGE];
	if (range->min == range->max) {
		expr->probability = 0.01;
	} else {
		expr->probability = 0.5;
	}

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

/** Optimize -type. */
static struct bfs_expr *optimize_type(struct bfs_opt *opt, struct bfs_expr *expr) {
	opt->after_true.types &= expr->num;
	opt->after_false.types &= ~expr->num;

	estimate_type_probability(expr);

	return expr;
}

/** Optimize -xtype. */
static struct bfs_expr *optimize_xtype(struct bfs_opt *opt, struct bfs_expr *expr) {
	if (opt->ctx->optlevel >= 4) {
		// Since -xtype dereferences symbolic links, it may have side
		// effects such as reporting permission errors, and thus
		// shouldn't be re-ordered without aggressive optimizations
		expr->pure = true;
	}

	opt->after_true.xtypes &= expr->num;
	opt->after_false.xtypes &= ~expr->num;

	estimate_type_probability(expr);

	return expr;
}

/**
 * Table of pure expressions.
 */
static bfs_eval_fn *const opt_pure[] = {
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

/**
 * Table of always-true expressions.
 */
static bfs_eval_fn *const opt_always_true[] = {
	eval_fls,
	eval_fprint,
	eval_fprint0,
	eval_fprintf,
	eval_fprintx,
	eval_prune,
	eval_true,

	// Non-returning
	eval_exit,
	eval_quit,
};

/**
 * Table of always-false expressions.
 */
static bfs_eval_fn *const opt_always_false[] = {
	eval_false,

	// Non-returning
	eval_exit,
	eval_quit,
};

#define FAST_COST       40.0
#define FNMATCH_COST   400.0
#define STAT_COST     1000.0
#define PRINT_COST   20000.0

/**
 * Table of expression costs.
 */
static const struct {
	/** The evaluation function with this cost. */
	bfs_eval_fn *eval_fn;
	/** The matching cost. */
	float cost;
} opt_costs[] = {
	{eval_access,    STAT_COST},
	{eval_acl,       STAT_COST},
	{eval_capable,   STAT_COST},
	{eval_empty, 2 * STAT_COST}, // readdir() is worse than stat()
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

/**
 * Table of expression probabilities.
 */
static const struct {
	/** The evaluation function with this cost. */
	bfs_eval_fn *eval_fn;
	/** The matching probability. */
	float probability;
} opt_probs[] = {
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

/**
 * Table of simple predicates.
 */
static const struct {
	/** The evaluation function this optimizer applies to. */
	bfs_eval_fn *eval_fn;
	/** The corresponding predicate. */
	enum pred_type pred;
} opt_preds[] = {
	{eval_acl,         ACL_PRED},
	{eval_capable, CAPABLE_PRED},
	{eval_empty,     EMPTY_PRED},
	{eval_hidden,   HIDDEN_PRED},
	{eval_nogroup, NOGROUP_PRED},
	{eval_nouser,   NOUSER_PRED},
	{eval_sparse,   SPARSE_PRED},
	{eval_xattr,     XATTR_PRED},
};

/**
 * Table of simple range comparisons.
 */
static const struct {
	/** The evaluation function this optimizer applies to. */
	bfs_eval_fn *eval_fn;
	/** The corresponding range. */
	enum range_type range;
} opt_ranges[] = {
	{eval_depth, DEPTH_RANGE},
	{eval_gid,     GID_RANGE},
	{eval_inum,   INUM_RANGE},
	{eval_links, LINKS_RANGE},
	{eval_size,   SIZE_RANGE},
	{eval_uid,     UID_RANGE},
};

/** Signature for custom optimizer functions. */
typedef struct bfs_expr *bfs_opt_fn(struct bfs_opt *opt, struct bfs_expr *expr);

/** Table of custom optimizer functions. */
static const struct {
	/** The evaluation function this optimizer applies to. */
	bfs_eval_fn *eval_fn;
	/** The corresponding optimizer function. */
	bfs_opt_fn *opt_fn;
} opt_fns[] = {
	// Primaries
	{eval_access,   optimize_access},
	{eval_empty,    optimize_empty},
	{eval_exec,     optimize_exec},
	{eval_gid,      optimize_gid},
	{eval_inum,     optimize_inum},
	{eval_links,    optimize_links},
	{eval_lname,    optimize_fnmatch},
	{eval_name,     optimize_fnmatch},
	{eval_path,     optimize_fnmatch},
	{eval_samefile, optimize_samefile},
	{eval_size,     optimize_size},
	{eval_type,     optimize_type},
	{eval_uid,      optimize_uid},
	{eval_xtype,    optimize_xtype},

	// Operators
	{eval_and,   optimize_and_expr_recursive},
	{eval_comma, optimize_comma_expr_recursive},
	{eval_not,   optimize_not_expr_recursive},
	{eval_or,    optimize_or_expr_recursive},
};

/**
 * Look up the appropriate optimizer for an expression and call it.
 */
static struct bfs_expr *optimize_expr_lookup(struct bfs_opt *opt, struct bfs_expr *expr) {
	for (size_t i = 0; i < countof(opt_pure); ++i) {
		if (opt_pure[i] == expr->eval_fn) {
			expr->pure = true;
			break;
		}
	}

	for (size_t i = 0; i < countof(opt_always_true); ++i) {
		if (opt_always_true[i] == expr->eval_fn) {
			expr->always_true = true;
			break;
		}
	}

	for (size_t i = 0; i < countof(opt_always_false); ++i) {
		if (opt_always_false[i] == expr->eval_fn) {
			expr->always_false = true;
			break;
		}
	}

	expr->cost = FAST_COST;
	for (size_t i = 0; i < countof(opt_costs); ++i) {
		if (opt_costs[i].eval_fn == expr->eval_fn) {
			expr->cost = opt_costs[i].cost;
			break;
		}
	}

	for (size_t i = 0; i < countof(opt_probs); ++i) {
		if (opt_probs[i].eval_fn == expr->eval_fn) {
			expr->probability = opt_probs[i].probability;
			break;
		}
	}

	for (size_t i = 0; i < countof(opt_preds); ++i) {
		if (opt_preds[i].eval_fn == expr->eval_fn) {
			opt_constrain_pred(opt, opt_preds[i].pred, true);
			break;
		}
	}

	for (size_t i = 0; i < countof(opt_ranges); ++i) {
		if (opt_ranges[i].eval_fn == expr->eval_fn) {
			optimize_icmp(opt, expr, opt_ranges[i].range);
			break;
		}
	}

	for (size_t i = 0; i < countof(opt_fns); ++i) {
		if (opt_fns[i].eval_fn == expr->eval_fn) {
			return opt_fns[i].opt_fn(opt, expr);
		}
	}

	return expr;
}

static struct bfs_expr *optimize_expr_recursive(struct bfs_opt *opt, struct bfs_expr *expr) {
	int optlevel = opt->ctx->optlevel;

	opt->after_true = opt->before;
	opt->after_false = opt->before;

	if (optlevel >= 2 && df_is_bottom(&opt->before)) {
		struct bfs_expr *ret = opt_const(false);
		opt_debug(opt, 2, "reachability: %pe --> %pe\n", expr, ret);
		opt_warning(opt, expr, "This expression is unreachable.\n\n");
		bfs_expr_free(expr);
		return ret;
	}

	expr = optimize_expr_lookup(opt, expr);
	if (!expr) {
		return NULL;
	}

	if (bfs_expr_is_parent(expr)) {
		struct bfs_expr *lhs = expr->lhs;
		struct bfs_expr *rhs = expr->rhs;
		if (rhs) {
			expr->persistent_fds = rhs->persistent_fds;
			expr->ephemeral_fds = rhs->ephemeral_fds;
		}
		if (lhs) {
			expr->persistent_fds += lhs->persistent_fds;
			if (lhs->ephemeral_fds > expr->ephemeral_fds) {
				expr->ephemeral_fds = lhs->ephemeral_fds;
			}
		}
	} else if (!expr->pure) {
		df_join(opt->impure, &opt->before);
	}

	if (expr->always_true) {
		expr->probability = 1.0;
		df_init_bottom(&opt->after_false);
	}
	if (expr->always_false) {
		expr->probability = 0.0;
		df_init_bottom(&opt->after_true);
	}

	if (optlevel < 2 || expr->eval_fn == eval_true || expr->eval_fn == eval_false) {
		return expr;
	}

	if (df_is_bottom(&opt->after_true)) {
		if (expr->pure) {
			struct bfs_expr *ret = opt_const(false);
			opt_warning(opt, expr, "This expression is always false.\n\n");
			opt_debug(opt, 2, "data flow: %pe --> %pe\n", expr, ret);
			bfs_expr_free(expr);
			return ret;
		} else {
			expr->always_false = true;
			expr->probability = 0.0;
		}
	} else if (df_is_bottom(&opt->after_false)) {
		if (expr->pure) {
			struct bfs_expr *ret = opt_const(true);
			opt_debug(opt, 2, "data flow: %pe --> %pe\n", expr, ret);
			opt_warning(opt, expr, "This expression is always true.\n\n");
			bfs_expr_free(expr);
			return ret;
		} else {
			expr->always_true = true;
			expr->probability = 1.0;
		}
	}

	return expr;
}

/** Swap the children of a binary expression if it would reduce the cost. */
static bool reorder_expr(const struct bfs_opt *opt, struct bfs_expr *expr, float swapped_cost) {
	if (swapped_cost < expr->cost) {
		bool debug = opt_debug(opt, 3, "cost: %pe <==> ", expr);
		struct bfs_expr *lhs = expr->lhs;
		expr->lhs = expr->rhs;
		expr->rhs = lhs;
		if (debug) {
			cfprintf(opt->ctx->cerr, "%pe (~${ylw}%g${rs} --> ~${ylw}%g${rs})\n", expr, expr->cost, swapped_cost);
		}
		expr->cost = swapped_cost;
		return true;
	} else {
		return false;
	}
}

/**
 * Recursively reorder sub-expressions to reduce the overall cost.
 *
 * @param expr
 *         The expression to optimize.
 * @return
 *         Whether any subexpression was reordered.
 */
static bool reorder_expr_recursive(const struct bfs_opt *opt, struct bfs_expr *expr) {
	if (!bfs_expr_is_parent(expr)) {
		return false;
	}

	struct bfs_expr *lhs = expr->lhs;
	struct bfs_expr *rhs = expr->rhs;

	bool ret = false;
	if (lhs) {
		ret |= reorder_expr_recursive(opt, lhs);
	}
	if (rhs) {
		ret |= reorder_expr_recursive(opt, rhs);
	}

	if (expr->eval_fn == eval_and || expr->eval_fn == eval_or) {
		if (lhs->pure && rhs->pure) {
			float rhs_prob = expr->eval_fn == eval_and ? rhs->probability : 1.0 - rhs->probability;
			float swapped_cost = rhs->cost + rhs_prob * lhs->cost;
			ret |= reorder_expr(opt, expr, swapped_cost);
		}
	}

	return ret;
}

/**
 * Optimize a top-level expression.
 */
static struct bfs_expr *optimize_expr(struct bfs_opt *opt, struct bfs_expr *expr) {
	struct df_domain saved_impure = *opt->impure;

	expr = optimize_expr_recursive(opt, expr);
	if (!expr) {
		return NULL;
	}

	if (opt->ctx->optlevel >= 3 && reorder_expr_recursive(opt, expr)) {
		// Re-do optimizations to account for the new ordering
		*opt->impure = saved_impure;
		expr = optimize_expr_recursive(opt, expr);
		if (!expr) {
			return NULL;
		}
	}

	return expr;
}

int bfs_optimize(struct bfs_ctx *ctx) {
	bfs_ctx_dump(ctx, DEBUG_OPT);

	struct df_domain impure;
	df_init_bottom(&impure);

	struct bfs_opt opt = {
		.ctx = ctx,
		.impure = &impure,
	};
	df_init_top(&opt.before);

	ctx->exclude = optimize_expr(&opt, ctx->exclude);
	if (!ctx->exclude) {
		return -1;
	}

	// Only non-excluded files are evaluated
	opt.before = opt.after_false;

	struct df_range *depth = &opt.before.ranges[DEPTH_RANGE];
	constrain_min(depth, ctx->mindepth);
	constrain_max(depth, ctx->maxdepth);

	ctx->expr = optimize_expr(&opt, ctx->expr);
	if (!ctx->expr) {
		return -1;
	}

	ctx->expr = ignore_result(&opt, ctx->expr);

	if (df_is_bottom(&impure)) {
		bfs_warning(ctx, "This command won't do anything.\n\n");
	}

	const struct df_range *impure_depth = &impure.ranges[DEPTH_RANGE];
	long long mindepth = impure_depth->min;
	long long maxdepth = impure_depth->max;

	int optlevel = ctx->optlevel;

	if (optlevel >= 2 && mindepth > ctx->mindepth) {
		if (mindepth > INT_MAX) {
			mindepth = INT_MAX;
		}
		ctx->mindepth = mindepth;
		opt_debug(&opt, 2, "data flow: mindepth --> %d\n", ctx->mindepth);
	}

	if (optlevel >= 4 && maxdepth < ctx->maxdepth) {
		if (maxdepth < INT_MIN) {
			maxdepth = INT_MIN;
		}
		ctx->maxdepth = maxdepth;
		opt_debug(&opt, 4, "data flow: maxdepth --> %d\n", ctx->maxdepth);
	}

	return 0;
}
