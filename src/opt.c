// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

/**
 * The expression optimizer.  Different optimization levels are supported:
 *
 * -O1: basic logical simplifications, like folding (-true -and -foo) to -foo.
 *
 * -O2: dead code elimination and data flow analysis.  struct opt_facts is used
 * to record data flow facts that are true at various points of evaluation.
 * Specifically, struct opt_facts records the facts that must be true before an
 * expression is evaluated (state->facts), and those that must be true after the
 * expression is evaluated, given that it returns true (state->facts_when_true)
 * or false (state->facts_when_true).  Additionally, state->facts_when_impure
 * records the possible data flow facts before any expressions with side effects
 * are evaluated.
 *
 * -O3: expression re-ordering to reduce expected cost.  In an expression like
 * (-foo -and -bar), if both -foo and -bar are pure (no side effects), they can
 * be re-ordered to (-bar -and -foo).  This is profitable if the expected cost
 * is lower for the re-ordered expression, for example if -foo is very slow or
 * -bar is likely to return false.
 *
 * -O4/-Ofast: aggressive optimizations that may affect correctness in corner
 * cases.  The main effect is to use facts_when_impure to determine if any side-
 * effects are reachable at all, and skipping the traversal if not.
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
 * A contrained integer range.
 */
struct range {
	/** The (inclusive) minimum value. */
	long long min;
	/** The (inclusive) maximum value. */
	long long max;
};

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
static void constrain_min(struct range *range, long long value) {
	range->min = max_value(range->min, value);
}

/** Contrain the maximum of a range. */
static void constrain_max(struct range *range, long long value) {
	range->max = min_value(range->max, value);
}

/** Remove a single value from a range. */
static void range_remove(struct range *range, long long value) {
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
static void range_union(struct range *result, const struct range *lhs, const struct range *rhs) {
	result->min = min_value(lhs->min, rhs->min);
	result->max = max_value(lhs->max, rhs->max);
}

/** Check if a range contains no values. */
static bool range_is_impossible(const struct range *range) {
	return range->min > range->max;
}

/** Set a range to contain no values. */
static void set_range_impossible(struct range *range) {
	range->min = LLONG_MAX;
	range->max = LLONG_MIN;
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
 * A possibly-known value of a predicate.
 */
enum known_pred {
	/** The state is impossible to reach. */
	PRED_IMPOSSIBLE = -2,
	/** The value of the predicate is not known. */
	PRED_UNKNOWN = -1,
	/** The predicate is known to be false. */
	PRED_FALSE = false,
	/** The predicate is known to be true. */
	PRED_TRUE = true,
};

/** Make a predicate known. */
static void constrain_pred(enum known_pred *pred, bool value) {
	if (*pred == PRED_UNKNOWN) {
		*pred = value;
	} else if (*pred == !value) {
		*pred = PRED_IMPOSSIBLE;
	}
}

/** Compute the union of two known predicates. */
static enum known_pred pred_union(enum known_pred lhs, enum known_pred rhs) {
	if (lhs == PRED_IMPOSSIBLE) {
		return rhs;
	} else if (rhs == PRED_IMPOSSIBLE) {
		return lhs;
	} else if (lhs == rhs) {
		return lhs;
	} else {
		return PRED_UNKNOWN;
	}
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

/**
 * Data flow facts about an evaluation point.
 */
struct opt_facts {
	/** The value ranges we track. */
	struct range ranges[RANGE_TYPES];

	/** The predicates we track. */
	enum known_pred preds[PRED_TYPES];

	/** Bitmask of possible file types. */
	unsigned int types;
	/** Bitmask of possible link target types. */
	unsigned int xtypes;
};

/** Initialize some data flow facts. */
static void facts_init(struct opt_facts *facts) {
	for (int i = 0; i < RANGE_TYPES; ++i) {
		struct range *range = &facts->ranges[i];
		range->min = 0; // All ranges we currently track are non-negative
		range->max = LLONG_MAX;
	}

	for (int i = 0; i < PRED_TYPES; ++i) {
		facts->preds[i] = PRED_UNKNOWN;
	}

	facts->types = ~0;
	facts->xtypes = ~0;
}

/** Compute the union of two fact sets. */
static void facts_union(struct opt_facts *result, const struct opt_facts *lhs, const struct opt_facts *rhs) {
	for (int i = 0; i < RANGE_TYPES; ++i) {
		range_union(&result->ranges[i], &lhs->ranges[i], &rhs->ranges[i]);
	}

	for (int i = 0; i < PRED_TYPES; ++i) {
		result->preds[i] = pred_union(lhs->preds[i], rhs->preds[i]);
	}

	result->types = lhs->types | rhs->types;
	result->xtypes = lhs->xtypes | rhs->xtypes;
}

/** Determine whether a fact set is impossible. */
static bool facts_are_impossible(const struct opt_facts *facts) {
	for (int i = 0; i < RANGE_TYPES; ++i) {
		if (range_is_impossible(&facts->ranges[i])) {
			return true;
		}
	}

	for (int i = 0; i < PRED_TYPES; ++i) {
		if (facts->preds[i] == PRED_IMPOSSIBLE) {
			return true;
		}
	}

	if (!facts->types || !facts->xtypes) {
		return true;
	}

	return false;
}

/** Set some facts to be impossible. */
static void set_facts_impossible(struct opt_facts *facts) {
	for (int i = 0; i < RANGE_TYPES; ++i) {
		set_range_impossible(&facts->ranges[i]);
	}

	for (int i = 0; i < PRED_TYPES; ++i) {
		facts->preds[i] = PRED_IMPOSSIBLE;
	}

	facts->types = 0;
	facts->xtypes = 0;
}

/**
 * Optimizer state.
 */
struct opt_state {
	/** The context we're optimizing. */
	const struct bfs_ctx *ctx;

	/** Data flow facts before this expression is evaluated. */
	struct opt_facts facts;
	/** Data flow facts after this expression returns true. */
	struct opt_facts facts_when_true;
	/** Data flow facts after this expression returns false. */
	struct opt_facts facts_when_false;
	/** Data flow facts before any side-effecting expressions are evaluated. */
	struct opt_facts *facts_when_impure;
};

/** Log an optimization. */
attr(printf(3, 4))
static bool opt_debug(const struct opt_state *state, int level, const char *format, ...) {
	bfs_assert(state->ctx->optlevel >= level);

	if (bfs_debug(state->ctx, DEBUG_OPT, "${cyn}-O%d${rs}: ", level)) {
		va_list args;
		va_start(args, format);
		cvfprintf(state->ctx->cerr, format, args);
		va_end(args);
		return true;
	} else {
		return false;
	}
}

/** Warn about an expression. */
attr(printf(3, 4))
static void opt_warning(const struct opt_state *state, const struct bfs_expr *expr, const char *format, ...) {
	if (bfs_expr_warning(state->ctx, expr)) {
		va_list args;
		va_start(args, format);
		bfs_vwarning(state->ctx, format, args);
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

static struct bfs_expr *optimize_not_expr(const struct opt_state *state, struct bfs_expr *expr);
static struct bfs_expr *optimize_and_expr(const struct opt_state *state, struct bfs_expr *expr);
static struct bfs_expr *optimize_or_expr(const struct opt_state *state, struct bfs_expr *expr);

/**
 * Apply De Morgan's laws.
 */
static struct bfs_expr *de_morgan(const struct opt_state *state, struct bfs_expr *expr, char **argv) {
	bool debug = opt_debug(state, 1, "De Morgan's laws: %pe ", expr);

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
		cfprintf(state->ctx->cerr, "<==> %pe\n", parent);
	}

	if (expr->lhs->eval_fn == eval_not) {
		expr->lhs = optimize_not_expr(state, expr->lhs);
	}
	if (expr->rhs->eval_fn == eval_not) {
		expr->rhs = optimize_not_expr(state, expr->rhs);
	}
	if (!expr->lhs || !expr->rhs) {
		bfs_expr_free(parent);
		return NULL;
	}

	if (expr->eval_fn == eval_and) {
		expr = optimize_and_expr(state, expr);
	} else {
		expr = optimize_or_expr(state, expr);
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
		parent = optimize_not_expr(state, parent);
	}
	return parent;
}

/** Optimize an expression recursively. */
static struct bfs_expr *optimize_expr_recursive(struct opt_state *state, struct bfs_expr *expr);

/**
 * Optimize a negation.
 */
static struct bfs_expr *optimize_not_expr(const struct opt_state *state, struct bfs_expr *expr) {
	bfs_assert(expr->eval_fn == eval_not);

	struct bfs_expr *rhs = expr->rhs;

	int optlevel = state->ctx->optlevel;
	if (optlevel >= 1) {
		if (rhs->eval_fn == eval_true || rhs->eval_fn == eval_false) {
			struct bfs_expr *ret = opt_const(rhs->eval_fn == eval_false);
			opt_debug(state, 1, "constant propagation: %pe <==> %pe\n", expr, ret);
			bfs_expr_free(expr);
			return ret;
		} else if (rhs->eval_fn == eval_not) {
			opt_debug(state, 1, "double negation: %pe <==> %pe\n", expr, rhs->rhs);
			return extract_child_expr(expr, &rhs->rhs);
		} else if (bfs_expr_never_returns(rhs)) {
			opt_debug(state, 1, "reachability: %pe <==> %pe\n", expr, rhs);
			return extract_child_expr(expr, &expr->rhs);
		} else if ((rhs->eval_fn == eval_and || rhs->eval_fn == eval_or)
			   && (rhs->lhs->eval_fn == eval_not || rhs->rhs->eval_fn == eval_not)) {
			return de_morgan(state, expr, expr->argv);
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
static struct bfs_expr *optimize_not_expr_recursive(struct opt_state *state, struct bfs_expr *expr) {
	struct opt_state rhs_state = *state;
	expr->rhs = optimize_expr_recursive(&rhs_state, expr->rhs);
	if (!expr->rhs) {
		goto fail;
	}

	state->facts_when_true = rhs_state.facts_when_false;
	state->facts_when_false = rhs_state.facts_when_true;

	return optimize_not_expr(state, expr);

fail:
	bfs_expr_free(expr);
	return NULL;
}

/** Optimize a conjunction. */
static struct bfs_expr *optimize_and_expr(const struct opt_state *state, struct bfs_expr *expr) {
	bfs_assert(expr->eval_fn == eval_and);

	struct bfs_expr *lhs = expr->lhs;
	struct bfs_expr *rhs = expr->rhs;

	const struct bfs_ctx *ctx = state->ctx;
	int optlevel = ctx->optlevel;
	if (optlevel >= 1) {
		if (lhs->eval_fn == eval_true) {
			opt_debug(state, 1, "conjunction elimination: %pe <==> %pe\n", expr, rhs);
			return extract_child_expr(expr, &expr->rhs);
		} else if (rhs->eval_fn == eval_true) {
			opt_debug(state, 1, "conjunction elimination: %pe <==> %pe\n", expr, lhs);
			return extract_child_expr(expr, &expr->lhs);
		} else if (lhs->always_false) {
			opt_debug(state, 1, "short-circuit: %pe <==> %pe\n", expr, lhs);
			opt_warning(state, expr->rhs, "This expression is unreachable.\n\n");
			return extract_child_expr(expr, &expr->lhs);
		} else if (lhs->always_true && rhs->eval_fn == eval_false) {
			bool debug = opt_debug(state, 1, "strength reduction: %pe <==> ", expr);
			struct bfs_expr *ret = extract_child_expr(expr, &expr->lhs);
			ret = negate_expr(ret, &fake_not_arg);
			if (debug && ret) {
				cfprintf(ctx->cerr, "%pe\n", ret);
			}
			return ret;
		} else if (optlevel >= 2 && lhs->pure && rhs->eval_fn == eval_false) {
			opt_debug(state, 2, "purity: %pe <==> %pe\n", expr, rhs);
			opt_warning(state, expr->lhs, "The result of this expression is ignored.\n\n");
			return extract_child_expr(expr, &expr->rhs);
		} else if (lhs->eval_fn == eval_not && rhs->eval_fn == eval_not) {
			return de_morgan(state, expr, expr->lhs->argv);
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
static struct bfs_expr *optimize_and_expr_recursive(struct opt_state *state, struct bfs_expr *expr) {
	struct opt_state lhs_state = *state;
	expr->lhs = optimize_expr_recursive(&lhs_state, expr->lhs);
	if (!expr->lhs) {
		goto fail;
	}

	struct opt_state rhs_state = *state;
	rhs_state.facts = lhs_state.facts_when_true;
	expr->rhs = optimize_expr_recursive(&rhs_state, expr->rhs);
	if (!expr->rhs) {
		goto fail;
	}

	state->facts_when_true = rhs_state.facts_when_true;
	facts_union(&state->facts_when_false, &lhs_state.facts_when_false, &rhs_state.facts_when_false);

	return optimize_and_expr(state, expr);

fail:
	bfs_expr_free(expr);
	return NULL;
}

/** Optimize a disjunction. */
static struct bfs_expr *optimize_or_expr(const struct opt_state *state, struct bfs_expr *expr) {
	bfs_assert(expr->eval_fn == eval_or);

	struct bfs_expr *lhs = expr->lhs;
	struct bfs_expr *rhs = expr->rhs;

	const struct bfs_ctx *ctx = state->ctx;
	int optlevel = ctx->optlevel;
	if (optlevel >= 1) {
		if (lhs->always_true) {
			opt_debug(state, 1, "short-circuit: %pe <==> %pe\n", expr, lhs);
			opt_warning(state, expr->rhs, "This expression is unreachable.\n\n");
			return extract_child_expr(expr, &expr->lhs);
		} else if (lhs->eval_fn == eval_false) {
			opt_debug(state, 1, "disjunctive syllogism: %pe <==> %pe\n", expr, rhs);
			return extract_child_expr(expr, &expr->rhs);
		} else if (rhs->eval_fn == eval_false) {
			opt_debug(state, 1, "disjunctive syllogism: %pe <==> %pe\n", expr, lhs);
			return extract_child_expr(expr, &expr->lhs);
		} else if (lhs->always_false && rhs->eval_fn == eval_true) {
			bool debug = opt_debug(state, 1, "strength reduction: %pe <==> ", expr);
			struct bfs_expr *ret = extract_child_expr(expr, &expr->lhs);
			ret = negate_expr(ret, &fake_not_arg);
			if (debug && ret) {
				cfprintf(ctx->cerr, "%pe\n", ret);
			}
			return ret;
		} else if (optlevel >= 2 && lhs->pure && rhs->eval_fn == eval_true) {
			opt_debug(state, 2, "purity: %pe <==> %pe\n", expr, rhs);
			opt_warning(state, expr->lhs, "The result of this expression is ignored.\n\n");
			return extract_child_expr(expr, &expr->rhs);
		} else if (lhs->eval_fn == eval_not && rhs->eval_fn == eval_not) {
			return de_morgan(state, expr, expr->lhs->argv);
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
static struct bfs_expr *optimize_or_expr_recursive(struct opt_state *state, struct bfs_expr *expr) {
	struct opt_state lhs_state = *state;
	expr->lhs = optimize_expr_recursive(&lhs_state, expr->lhs);
	if (!expr->lhs) {
		goto fail;
	}

	struct opt_state rhs_state = *state;
	rhs_state.facts = lhs_state.facts_when_false;
	expr->rhs = optimize_expr_recursive(&rhs_state, expr->rhs);
	if (!expr->rhs) {
		goto fail;
	}

	facts_union(&state->facts_when_true, &lhs_state.facts_when_true, &rhs_state.facts_when_true);
	state->facts_when_false = rhs_state.facts_when_false;

	return optimize_or_expr(state, expr);

fail:
	bfs_expr_free(expr);
	return NULL;
}

/** Optimize an expression in an ignored-result context. */
static struct bfs_expr *ignore_result(const struct opt_state *state, struct bfs_expr *expr) {
	int optlevel = state->ctx->optlevel;

	if (optlevel >= 1) {
		while (true) {
			if (expr->eval_fn == eval_not) {
				opt_debug(state, 1, "ignored result: %pe --> %pe\n", expr, expr->rhs);
				opt_warning(state, expr, "The result of this expression is ignored.\n\n");
				expr = extract_child_expr(expr, &expr->rhs);
			} else if (optlevel >= 2
			           && (expr->eval_fn == eval_and || expr->eval_fn == eval_or || expr->eval_fn == eval_comma)
			           && expr->rhs->pure) {
				opt_debug(state, 2, "ignored result: %pe --> %pe\n", expr, expr->lhs);
				opt_warning(state, expr->rhs, "The result of this expression is ignored.\n\n");
				expr = extract_child_expr(expr, &expr->lhs);
			} else {
				break;
			}
		}

		if (optlevel >= 2 && expr->pure && expr->eval_fn != eval_false) {
			struct bfs_expr *ret = opt_const(false);
			opt_debug(state, 2, "ignored result: %pe --> %pe\n", expr, ret);
			opt_warning(state, expr, "The result of this expression is ignored.\n\n");
			bfs_expr_free(expr);
			return ret;
		}
	}

	return expr;
}

/** Optimize a comma expression. */
static struct bfs_expr *optimize_comma_expr(const struct opt_state *state, struct bfs_expr *expr) {
	bfs_assert(expr->eval_fn == eval_comma);

	struct bfs_expr *lhs = expr->lhs;
	struct bfs_expr *rhs = expr->rhs;

	int optlevel = state->ctx->optlevel;
	if (optlevel >= 1) {
		lhs = expr->lhs = ignore_result(state, lhs);

		if (bfs_expr_never_returns(lhs)) {
			opt_debug(state, 1, "reachability: %pe <==> %pe\n", expr, lhs);
			opt_warning(state, expr->rhs, "This expression is unreachable.\n\n");
			return extract_child_expr(expr, &expr->lhs);
		} else if ((lhs->always_true && rhs->eval_fn == eval_true)
			   || (lhs->always_false && rhs->eval_fn == eval_false)) {
			opt_debug(state, 1, "redundancy elimination: %pe <==> %pe\n", expr, lhs);
			return extract_child_expr(expr, &expr->lhs);
		} else if (optlevel >= 2 && lhs->pure) {
			opt_debug(state, 2, "purity: %pe <==> %pe\n", expr, rhs);
			opt_warning(state, expr->lhs, "The result of this expression is ignored.\n\n");
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
static struct bfs_expr *optimize_comma_expr_recursive(struct opt_state *state, struct bfs_expr *expr) {
	struct opt_state lhs_state = *state;
	expr->lhs = optimize_expr_recursive(&lhs_state, expr->lhs);
	if (!expr->lhs) {
		goto fail;
	}

	struct opt_state rhs_state = *state;
	facts_union(&rhs_state.facts, &lhs_state.facts_when_true, &lhs_state.facts_when_false);
	expr->rhs = optimize_expr_recursive(&rhs_state, expr->rhs);
	if (!expr->rhs) {
		goto fail;
	}

	return optimize_comma_expr(state, expr);

fail:
	bfs_expr_free(expr);
	return NULL;
}

/** Infer data flow facts about a predicate. */
static void infer_pred_facts(struct opt_state *state, enum pred_type pred) {
	constrain_pred(&state->facts_when_true.preds[pred], true);
	constrain_pred(&state->facts_when_false.preds[pred], false);
}

/** Infer data flow facts about an icmp-style ([+-]N) expression. */
static void infer_icmp_facts(struct opt_state *state, const struct bfs_expr *expr, enum range_type type) {
	struct range *range_when_true = &state->facts_when_true.ranges[type];
	struct range *range_when_false = &state->facts_when_false.ranges[type];
	long long value = expr->num;

	switch (expr->int_cmp) {
	case BFS_INT_EQUAL:
		constrain_min(range_when_true, value);
		constrain_max(range_when_true, value);
		range_remove(range_when_false, value);
		break;

	case BFS_INT_LESS:
		constrain_min(range_when_false, value);
		constrain_max(range_when_true, value);
		range_remove(range_when_true, value);
		break;

	case BFS_INT_GREATER:
		constrain_max(range_when_false, value);
		constrain_min(range_when_true, value);
		range_remove(range_when_true, value);
		break;
	}
}

/** Optimize -{execut,read,writ}able. */
static struct bfs_expr *optimize_access(struct opt_state *state, struct bfs_expr *expr) {
	expr->probability = 1.0;

	if (expr->num & R_OK) {
		infer_pred_facts(state, READABLE_PRED);
		expr->probability *= 0.99;
	}

	if (expr->num & W_OK) {
		infer_pred_facts(state, WRITABLE_PRED);
		expr->probability *= 0.8;
	}

	if (expr->num & X_OK) {
		infer_pred_facts(state, EXECUTABLE_PRED);
		expr->probability *= 0.2;
	}

	return expr;
}

/** Optimize -empty. */
static struct bfs_expr *optimize_empty(struct opt_state *state, struct bfs_expr *expr) {
	if (state->ctx->optlevel >= 4) {
		// Since -empty attempts to open and read directories, it may
		// have side effects such as reporting permission errors, and
		// thus shouldn't be re-ordered without aggressive optimizations
		expr->pure = true;
	}

	return expr;
}

/** Optimize -{exec,ok}{,dir}. */
static struct bfs_expr *optimize_exec(struct opt_state *state, struct bfs_expr *expr) {
	if (expr->exec->flags & BFS_EXEC_MULTI) {
		expr->always_true = true;
	} else {
		expr->cost = 1000000.0;
	}

	return expr;
}

/** Optimize -name/-lname/-path. */
static struct bfs_expr *optimize_fnmatch(struct opt_state *state, struct bfs_expr *expr) {
	if (strchr(expr->argv[1], '*')) {
		expr->probability = 0.5;
	} else {
		expr->probability = 0.1;
	}

	return expr;
}

/** Optimize -gid. */
static struct bfs_expr *optimize_gid(struct opt_state *state, struct bfs_expr *expr) {
	struct range *range = &state->facts_when_true.ranges[GID_RANGE];
	if (range->min == range->max) {
		gid_t gid = range->min;
		bool nogroup = !bfs_getgrgid(state->ctx->groups, gid);
		if (errno == 0) {
			constrain_pred(&state->facts_when_true.preds[NOGROUP_PRED], nogroup);
		}
	}

	return expr;
}

/** Optimize -inum. */
static struct bfs_expr *optimize_inum(struct opt_state *state, struct bfs_expr *expr) {
	struct range *range = &state->facts_when_true.ranges[INUM_RANGE];
	if (range->min == range->max) {
		expr->probability = 0.01;
	} else {
		expr->probability = 0.5;
	}

	return expr;
}

/** Optimize -links. */
static struct bfs_expr *optimize_links(struct opt_state *state, struct bfs_expr *expr) {
	struct range *range = &state->facts_when_true.ranges[LINKS_RANGE];
	if (1 >= range->min && 1 <= range->max) {
		expr->probability = 0.99;
	} else {
		expr->probability = 0.5;
	}

	return expr;
}

/** Optimize -uid. */
static struct bfs_expr *optimize_uid(struct opt_state *state, struct bfs_expr *expr) {
	struct range *range = &state->facts_when_true.ranges[UID_RANGE];
	if (range->min == range->max) {
		uid_t uid = range->min;
		bool nouser = !bfs_getpwuid(state->ctx->users, uid);
		if (errno == 0) {
			constrain_pred(&state->facts_when_true.preds[NOUSER_PRED], nouser);
		}
	}

	return expr;
}

/** Optimize -samefile. */
static struct bfs_expr *optimize_samefile(struct opt_state *state, struct bfs_expr *expr) {
	struct range *range_when_true = &state->facts_when_true.ranges[INUM_RANGE];
	constrain_min(range_when_true, expr->ino);
	constrain_max(range_when_true, expr->ino);
	return expr;
}

/** Optimize -size. */
static struct bfs_expr *optimize_size(struct opt_state *state, struct bfs_expr *expr) {
	struct range *range = &state->facts_when_true.ranges[SIZE_RANGE];
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
static struct bfs_expr *optimize_type(struct opt_state *state, struct bfs_expr *expr) {
	state->facts_when_true.types &= expr->num;
	state->facts_when_false.types &= ~expr->num;

	estimate_type_probability(expr);

	return expr;
}

/** Optimize -xtype. */
static struct bfs_expr *optimize_xtype(struct opt_state *state, struct bfs_expr *expr) {
	if (state->ctx->optlevel >= 4) {
		// Since -xtype dereferences symbolic links, it may have side
		// effects such as reporting permission errors, and thus
		// shouldn't be re-ordered without aggressive optimizations
		expr->pure = true;
	}

	state->facts_when_true.xtypes &= expr->num;
	state->facts_when_false.xtypes &= ~expr->num;

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
typedef struct bfs_expr *bfs_opt_fn(struct opt_state *state, struct bfs_expr *expr);

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
static struct bfs_expr *optimize_expr_lookup(struct opt_state *state, struct bfs_expr *expr) {
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
			infer_pred_facts(state, opt_preds[i].pred);
			break;
		}
	}

	for (size_t i = 0; i < countof(opt_ranges); ++i) {
		if (opt_ranges[i].eval_fn == expr->eval_fn) {
			infer_icmp_facts(state, expr, opt_ranges[i].range);
			break;
		}
	}

	for (size_t i = 0; i < countof(opt_fns); ++i) {
		if (opt_fns[i].eval_fn == expr->eval_fn) {
			return opt_fns[i].opt_fn(state, expr);
		}
	}

	return expr;
}

static struct bfs_expr *optimize_expr_recursive(struct opt_state *state, struct bfs_expr *expr) {
	int optlevel = state->ctx->optlevel;

	state->facts_when_true = state->facts;
	state->facts_when_false = state->facts;

	if (optlevel >= 2 && facts_are_impossible(&state->facts)) {
		struct bfs_expr *ret = opt_const(false);
		opt_debug(state, 2, "reachability: %pe --> %pe\n", expr, ret);
		opt_warning(state, expr, "This expression is unreachable.\n\n");
		bfs_expr_free(expr);
		return ret;
	}

	expr = optimize_expr_lookup(state, expr);
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
		facts_union(state->facts_when_impure, state->facts_when_impure, &state->facts);
	}

	if (expr->always_true) {
		expr->probability = 1.0;
		set_facts_impossible(&state->facts_when_false);
	}
	if (expr->always_false) {
		expr->probability = 0.0;
		set_facts_impossible(&state->facts_when_true);
	}

	if (optlevel < 2 || expr->eval_fn == eval_true || expr->eval_fn == eval_false) {
		return expr;
	}

	if (facts_are_impossible(&state->facts_when_true)) {
		if (expr->pure) {
			struct bfs_expr *ret = opt_const(false);
			opt_warning(state, expr, "This expression is always false.\n\n");
			opt_debug(state, 2, "data flow: %pe --> %pe\n", expr, ret);
			bfs_expr_free(expr);
			return ret;
		} else {
			expr->always_false = true;
			expr->probability = 0.0;
		}
	} else if (facts_are_impossible(&state->facts_when_false)) {
		if (expr->pure) {
			struct bfs_expr *ret = opt_const(true);
			opt_debug(state, 2, "data flow: %pe --> %pe\n", expr, ret);
			opt_warning(state, expr, "This expression is always true.\n\n");
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
static bool reorder_expr(const struct opt_state *state, struct bfs_expr *expr, float swapped_cost) {
	if (swapped_cost < expr->cost) {
		bool debug = opt_debug(state, 3, "cost: %pe <==> ", expr);
		struct bfs_expr *lhs = expr->lhs;
		expr->lhs = expr->rhs;
		expr->rhs = lhs;
		if (debug) {
			cfprintf(state->ctx->cerr, "%pe (~${ylw}%g${rs} --> ~${ylw}%g${rs})\n", expr, expr->cost, swapped_cost);
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
static bool reorder_expr_recursive(const struct opt_state *state, struct bfs_expr *expr) {
	if (!bfs_expr_is_parent(expr)) {
		return false;
	}

	struct bfs_expr *lhs = expr->lhs;
	struct bfs_expr *rhs = expr->rhs;

	bool ret = false;
	if (lhs) {
		ret |= reorder_expr_recursive(state, lhs);
	}
	if (rhs) {
		ret |= reorder_expr_recursive(state, rhs);
	}

	if (expr->eval_fn == eval_and || expr->eval_fn == eval_or) {
		if (lhs->pure && rhs->pure) {
			float rhs_prob = expr->eval_fn == eval_and ? rhs->probability : 1.0 - rhs->probability;
			float swapped_cost = rhs->cost + rhs_prob * lhs->cost;
			ret |= reorder_expr(state, expr, swapped_cost);
		}
	}

	return ret;
}

/**
 * Optimize a top-level expression.
 */
static struct bfs_expr *optimize_expr(struct opt_state *state, struct bfs_expr *expr) {
	struct opt_facts saved_impure = *state->facts_when_impure;

	expr = optimize_expr_recursive(state, expr);
	if (!expr) {
		return NULL;
	}

	if (state->ctx->optlevel >= 3 && reorder_expr_recursive(state, expr)) {
		// Re-do optimizations to account for the new ordering
		*state->facts_when_impure = saved_impure;
		expr = optimize_expr_recursive(state, expr);
		if (!expr) {
			return NULL;
		}
	}

	return expr;
}

int bfs_optimize(struct bfs_ctx *ctx) {
	bfs_ctx_dump(ctx, DEBUG_OPT);

	struct opt_facts facts_when_impure;
	set_facts_impossible(&facts_when_impure);

	struct opt_state state = {
		.ctx = ctx,
		.facts_when_impure = &facts_when_impure,
	};
	facts_init(&state.facts);

	ctx->exclude = optimize_expr(&state, ctx->exclude);
	if (!ctx->exclude) {
		return -1;
	}

	// Only non-excluded files are evaluated
	state.facts = state.facts_when_false;

	struct range *depth = &state.facts.ranges[DEPTH_RANGE];
	constrain_min(depth, ctx->mindepth);
	constrain_max(depth, ctx->maxdepth);

	ctx->expr = optimize_expr(&state, ctx->expr);
	if (!ctx->expr) {
		return -1;
	}

	ctx->expr = ignore_result(&state, ctx->expr);

	if (facts_are_impossible(&facts_when_impure)) {
		bfs_warning(ctx, "This command won't do anything.\n\n");
	}

	const struct range *depth_when_impure = &facts_when_impure.ranges[DEPTH_RANGE];
	long long mindepth = depth_when_impure->min;
	long long maxdepth = depth_when_impure->max;

	int optlevel = ctx->optlevel;

	if (optlevel >= 2 && mindepth > ctx->mindepth) {
		if (mindepth > INT_MAX) {
			mindepth = INT_MAX;
		}
		ctx->mindepth = mindepth;
		opt_debug(&state, 2, "data flow: mindepth --> %d\n", ctx->mindepth);
	}

	if (optlevel >= 4 && maxdepth < ctx->maxdepth) {
		if (maxdepth < INT_MIN) {
			maxdepth = INT_MIN;
		}
		ctx->maxdepth = maxdepth;
		opt_debug(&state, 4, "data flow: maxdepth --> %d\n", ctx->maxdepth);
	}

	return 0;
}
