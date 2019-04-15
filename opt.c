/****************************************************************************
 * bfs                                                                      *
 * Copyright (C) 2017-2019 Tavian Barnes <tavianator@tavianator.com>        *
 *                                                                          *
 * Permission to use, copy, modify, and/or distribute this software for any *
 * purpose with or without fee is hereby granted.                           *
 *                                                                          *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES *
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF         *
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR  *
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES   *
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN    *
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF  *
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.           *
 ****************************************************************************/

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

#include "cmdline.h"
#include "color.h"
#include "eval.h"
#include "expr.h"
#include <assert.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>

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
static bool range_impossible(const struct range *range) {
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
	MAX_RANGE,
};

/**
 * Data flow facts about an evaluation point.
 */
struct opt_facts {
	/** The value ranges we track. */
	struct range ranges[MAX_RANGE];

	/** Bitmask of possible file types. */
	enum bftw_typeflag types;
	/** Bitmask of possible link target types. */
	enum bftw_typeflag xtypes;
};

/** Initialize some data flow facts. */
static void facts_init(struct opt_facts *facts) {
	for (int i = 0; i < MAX_RANGE; ++i) {
		struct range *range = facts->ranges + i;
		range->min = 0; // All ranges we currently track are non-negative
		range->max = LLONG_MAX;
	}

	facts->types = ~0;
	facts->xtypes = ~0;
}

/** Compute the union of two fact sets. */
static void facts_union(struct opt_facts *result, const struct opt_facts *lhs, const struct opt_facts *rhs) {
	for (int i = 0; i < MAX_RANGE; ++i) {
		range_union(result->ranges + i, lhs->ranges + i, rhs->ranges + i);
	}

	result->types = lhs->types | rhs->types;
	result->xtypes = lhs->xtypes | rhs->xtypes;
}

/** Determine whether a fact set is impossible. */
static bool facts_impossible(const struct opt_facts *facts) {
	for (int i = 0; i < MAX_RANGE; ++i) {
		if (range_impossible(facts->ranges + i)) {
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
	for (int i = 0; i < MAX_RANGE; ++i) {
		set_range_impossible(facts->ranges + i);
	}

	facts->types = 0;
	facts->xtypes = 0;
}

/**
 * Optimizer state.
 */
struct opt_state {
	/** The command line we're optimizing. */
	const struct cmdline *cmdline;

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
static void debug_opt(const struct opt_state *state, const char *format, ...) {
	if (!(state->cmdline->debug & DEBUG_OPT)) {
		return;
	}

	CFILE *cerr = state->cmdline->cerr;

	va_list args;
	va_start(args, format);

	for (const char *i = format; *i != '\0'; ++i) {
		if (*i == '%') {
			switch (*++i) {
			case 'd':
				fprintf(cerr->file, "%d", va_arg(args, int));
				break;

			case 'e':
				dump_expr(cerr, va_arg(args, const struct expr *), false);
				break;

			case 'g':
				cfprintf(cerr, "${ylw}%g${rs}", va_arg(args, double));
				break;

			default:
				assert(false);
				break;
			}
		} else {
			fputc(*i, stderr);
		}
	}

	va_end(args);
}

/** Extract a child expression, freeing the outer expression. */
static struct expr *extract_child_expr(struct expr *expr, struct expr **child) {
	struct expr *ret = *child;
	*child = NULL;
	free_expr(expr);
	return ret;
}

/**
 * Negate an expression.
 */
static struct expr *negate_expr(struct expr *rhs, char **argv) {
	if (rhs->eval == eval_not) {
		return extract_child_expr(rhs, &rhs->rhs);
	}

	struct expr *expr = new_expr(eval_not, 1, argv);
	if (!expr) {
		free_expr(rhs);
		return NULL;
	}

	expr->rhs = rhs;
	return expr;
}

static struct expr *optimize_not_expr(const struct opt_state *state, struct expr *expr);
static struct expr *optimize_and_expr(const struct opt_state *state, struct expr *expr);
static struct expr *optimize_or_expr(const struct opt_state *state, struct expr *expr);

/**
 * Apply De Morgan's laws.
 */
static struct expr *de_morgan(const struct opt_state *state, struct expr *expr, char **argv) {
	debug_opt(state, "-O1: De Morgan's laws: %e ", expr);

	struct expr *parent = negate_expr(expr, argv);
	if (!parent) {
		return NULL;
	}

	bool has_parent = true;
	if (parent->eval != eval_not) {
		expr = parent;
		has_parent = false;
	}

	if (expr->eval == eval_and) {
		expr->eval = eval_or;
		expr->argv = &fake_or_arg;
	} else {
		assert(expr->eval == eval_or);
		expr->eval = eval_and;
		expr->argv = &fake_and_arg;
	}

	expr->lhs = negate_expr(expr->lhs, argv);
	expr->rhs = negate_expr(expr->rhs, argv);
	if (!expr->lhs || !expr->rhs) {
		free_expr(parent);
		return NULL;
	}

	debug_opt(state, "<==> %e\n", parent);

	if (expr->lhs->eval == eval_not) {
		expr->lhs = optimize_not_expr(state, expr->lhs);
	}
	if (expr->rhs->eval == eval_not) {
		expr->rhs = optimize_not_expr(state, expr->rhs);
	}
	if (!expr->lhs || !expr->rhs) {
		free_expr(parent);
		return NULL;
	}

	if (expr->eval == eval_and) {
		expr = optimize_and_expr(state, expr);
	} else {
		expr = optimize_or_expr(state, expr);
	}
	if (!expr) {
		if (has_parent) {
			parent->rhs = NULL;
			free_expr(parent);
		}
		return NULL;
	}

	if (has_parent) {
		parent = optimize_not_expr(state, parent);
	}
	return parent;
}

/** Optimize an expression recursively. */
static struct expr *optimize_expr_recursive(struct opt_state *state, struct expr *expr);

/**
 * Optimize a negation.
 */
static struct expr *optimize_not_expr(const struct opt_state *state, struct expr *expr) {
	assert(expr->eval == eval_not);

	struct expr *rhs = expr->rhs;

	int optlevel = state->cmdline->optlevel;
	if (optlevel >= 1) {
		if (rhs == &expr_true) {
			debug_opt(state, "-O1: constant propagation: %e <==> %e\n", expr, &expr_false);
			free_expr(expr);
			return &expr_false;
		} else if (rhs == &expr_false) {
			debug_opt(state, "-O1: constant propagation: %e <==> %e\n", expr, &expr_true);
			free_expr(expr);
			return &expr_true;
		} else if (rhs->eval == eval_not) {
			debug_opt(state, "-O1: double negation: %e <==> %e\n", expr, rhs->rhs);
			return extract_child_expr(expr, &rhs->rhs);
		} else if (expr_never_returns(rhs)) {
			debug_opt(state, "-O1: reachability: %e <==> %e\n", expr, rhs);
			return extract_child_expr(expr, &expr->rhs);
		} else if ((rhs->eval == eval_and || rhs->eval == eval_or)
			   && (rhs->lhs->eval == eval_not || rhs->rhs->eval == eval_not)) {
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
static struct expr *optimize_not_expr_recursive(struct opt_state *state, struct expr *expr) {
	struct opt_state rhs_state = *state;
	expr->rhs = optimize_expr_recursive(&rhs_state, expr->rhs);
	if (!expr->rhs) {
		goto fail;
	}

	state->facts_when_true = rhs_state.facts_when_false;
	state->facts_when_false = rhs_state.facts_when_true;

	return optimize_not_expr(state, expr);

fail:
	free_expr(expr);
	return NULL;
}

/** Optimize a conjunction. */
static struct expr *optimize_and_expr(const struct opt_state *state, struct expr *expr) {
	assert(expr->eval == eval_and);

	struct expr *lhs = expr->lhs;
	struct expr *rhs = expr->rhs;

	int optlevel = state->cmdline->optlevel;
	if (optlevel >= 1) {
		if (lhs == &expr_true) {
			debug_opt(state, "-O1: conjunction elimination: %e <==> %e\n", expr, rhs);
			return extract_child_expr(expr, &expr->rhs);
		} else if (rhs == &expr_true) {
			debug_opt(state, "-O1: conjunction elimination: %e <==> %e\n", expr, lhs);
			return extract_child_expr(expr, &expr->lhs);
		} else if (lhs->always_false) {
			debug_opt(state, "-O1: short-circuit: %e <==> %e\n", expr, lhs);
			return extract_child_expr(expr, &expr->lhs);
		} else if (lhs->always_true && rhs == &expr_false) {
			debug_opt(state, "-O1: strength reduction: %e <==> ", expr);
			struct expr *ret = extract_child_expr(expr, &expr->lhs);
			ret = negate_expr(ret, &fake_not_arg);
			if (ret) {
				debug_opt(state, "%e\n", ret);
			}
			return ret;
		} else if (optlevel >= 2 && lhs->pure && rhs == &expr_false) {
			debug_opt(state, "-O2: purity: %e <==> %e\n", expr, rhs);
			return extract_child_expr(expr, &expr->rhs);
		} else if (lhs->eval == eval_not && rhs->eval == eval_not) {
			return de_morgan(state, expr, expr->lhs->argv);
		}
	}

	expr->pure = lhs->pure && rhs->pure;
	expr->always_true = lhs->always_true && rhs->always_true;
	expr->always_false = lhs->always_false || rhs->always_false;
	expr->cost = lhs->cost + lhs->probability*rhs->cost;
	expr->probability = lhs->probability*rhs->probability;

	return expr;
}

/** Optimize a conjunction recursively. */
static struct expr *optimize_and_expr_recursive(struct opt_state *state, struct expr *expr) {
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
	free_expr(expr);
	return NULL;
}

/** Optimize a disjunction. */
static struct expr *optimize_or_expr(const struct opt_state *state, struct expr *expr) {
	assert(expr->eval == eval_or);

	struct expr *lhs = expr->lhs;
	struct expr *rhs = expr->rhs;

	int optlevel = state->cmdline->optlevel;
	if (optlevel >= 1) {
		if (lhs->always_true) {
			debug_opt(state, "-O1: short-circuit: %e <==> %e\n", expr, lhs);
			return extract_child_expr(expr, &expr->lhs);
		} else if (lhs == &expr_false) {
			debug_opt(state, "-O1: disjunctive syllogism: %e <==> %e\n", expr, rhs);
			return extract_child_expr(expr, &expr->rhs);
		} else if (rhs == &expr_false) {
			debug_opt(state, "-O1: disjunctive syllogism: %e <==> %e\n", expr, lhs);
			return extract_child_expr(expr, &expr->lhs);
		} else if (lhs->always_false && rhs == &expr_true) {
			debug_opt(state, "-O1: strength reduction: %e <==> ", expr);
			struct expr *ret = extract_child_expr(expr, &expr->lhs);
			ret = negate_expr(ret, &fake_not_arg);
			if (ret) {
				debug_opt(state, "%e\n", ret);
			}
			return ret;
		} else if (optlevel >= 2 && lhs->pure && rhs == &expr_true) {
			debug_opt(state, "-O2: purity: %e <==> %e\n", expr, rhs);
			return extract_child_expr(expr, &expr->rhs);
		} else if (lhs->eval == eval_not && rhs->eval == eval_not) {
			return de_morgan(state, expr, expr->lhs->argv);
		}
	}

	expr->pure = lhs->pure && rhs->pure;
	expr->always_true = lhs->always_true || rhs->always_true;
	expr->always_false = lhs->always_false && rhs->always_false;
	expr->cost = lhs->cost + (1 - lhs->probability)*rhs->cost;
	expr->probability = lhs->probability + rhs->probability - lhs->probability*rhs->probability;

	return expr;
}

/** Optimize a disjunction recursively. */
static struct expr *optimize_or_expr_recursive(struct opt_state *state, struct expr *expr) {
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
	free_expr(expr);
	return NULL;
}

/** Optimize an expression in an ignored-result context. */
static struct expr *ignore_result(const struct opt_state *state, struct expr *expr) {
	int optlevel = state->cmdline->optlevel;

	if (optlevel >= 1) {
		while (true) {
			if (expr->eval == eval_not) {
				debug_opt(state, "-O1: ignored result: %e --> %e\n", expr, expr->rhs);
				expr = extract_child_expr(expr, &expr->rhs);
			} else if (optlevel >= 2
			           && (expr->eval == eval_and || expr->eval == eval_or || expr->eval == eval_comma)
			           && expr->rhs->pure) {
				debug_opt(state, "-O2: ignored result: %e --> %e\n", expr, expr->lhs);
				expr = extract_child_expr(expr, &expr->lhs);
			} else {
				break;
			}
		}

		if (optlevel >= 2 && expr->pure && expr != &expr_false) {
			debug_opt(state, "-O2: ignored result: %e --> %e\n", expr, &expr_false);
			free_expr(expr);
			expr = &expr_false;
		}
	}

	return expr;
}

/** Optimize a comma expression. */
static struct expr *optimize_comma_expr(const struct opt_state *state, struct expr *expr) {
	assert(expr->eval == eval_comma);

	struct expr *lhs = expr->lhs;
	struct expr *rhs = expr->rhs;

	int optlevel = state->cmdline->optlevel;
	if (optlevel >= 1) {
		lhs = expr->lhs = ignore_result(state, lhs);

		if (expr_never_returns(lhs)) {
			debug_opt(state, "-O1: reachability: %e <==> %e\n", expr, lhs);
			return extract_child_expr(expr, &expr->lhs);
		} else if ((lhs->always_true && rhs == &expr_true)
			   || (lhs->always_false && rhs == &expr_false)) {
			debug_opt(state, "-O1: redundancy elimination: %e <==> %e\n", expr, lhs);
			return extract_child_expr(expr, &expr->lhs);
		} else if (optlevel >= 2 && lhs->pure) {
			debug_opt(state, "-O2: purity: %e <==> %e\n", expr, rhs);
			return extract_child_expr(expr, &expr->rhs);
		}
	}

	expr->pure = lhs->pure && rhs->pure;
	expr->always_true = expr_never_returns(lhs) || rhs->always_true;
	expr->always_false = expr_never_returns(lhs) || rhs->always_false;
	expr->cost = lhs->cost + rhs->cost;
	expr->probability = rhs->probability;

	return expr;
}

/** Optimize a comma expression recursively. */
static struct expr *optimize_comma_expr_recursive(struct opt_state *state, struct expr *expr) {
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
	free_expr(expr);
	return NULL;
}

/** Infer data flow facts about an icmp-style ([+-]N) expression */
static void infer_icmp_facts(struct opt_state *state, const struct expr *expr, enum range_type type) {
	struct range *range_when_true = state->facts_when_true.ranges + type;
	struct range *range_when_false = state->facts_when_false.ranges + type;
	long long value = expr->idata;

	switch (expr->cmp_flag) {
	case CMP_EXACT:
		constrain_min(range_when_true, value);
		constrain_max(range_when_true, value);
		range_remove(range_when_false, value);
		break;

	case CMP_LESS:
		constrain_min(range_when_false, value);
		constrain_max(range_when_true, value);
		range_remove(range_when_true, value);
		break;

	case CMP_GREATER:
		constrain_max(range_when_false, value);
		constrain_min(range_when_true, value);
		range_remove(range_when_true, value);
		break;
	}
}

/** Infer data flow facts about a -samefile expression. */
static void infer_samefile_facts(struct opt_state *state, const struct expr *expr) {
	struct range *range_when_true = state->facts_when_true.ranges + INUM_RANGE;
	constrain_min(range_when_true, expr->ino);
	constrain_max(range_when_true, expr->ino);
}

/** Infer data flow facts about a -type expression. */
static void infer_type_facts(struct opt_state *state, const struct expr *expr) {
	state->facts_when_true.types &= expr->idata;
	state->facts_when_false.types &= ~expr->idata;
}

/** Infer data flow facts about an -xtype expression. */
static void infer_xtype_facts(struct opt_state *state, const struct expr *expr) {
	state->facts_when_true.xtypes &= expr->idata;
	state->facts_when_false.xtypes &= ~expr->idata;
}

static struct expr *optimize_expr_recursive(struct opt_state *state, struct expr *expr) {
	state->facts_when_true = state->facts;
	state->facts_when_false = state->facts;

	if (expr->eval == eval_depth) {
		infer_icmp_facts(state, expr, DEPTH_RANGE);
	} else if (expr->eval == eval_gid) {
		infer_icmp_facts(state, expr, GID_RANGE);
	} else if (expr->eval == eval_inum) {
		infer_icmp_facts(state, expr, INUM_RANGE);
	} else if (expr->eval == eval_links) {
		infer_icmp_facts(state, expr, LINKS_RANGE);
	} else if (expr->eval == eval_samefile) {
		infer_samefile_facts(state, expr);
	} else if (expr->eval == eval_size) {
		infer_icmp_facts(state, expr, SIZE_RANGE);
	} else if (expr->eval == eval_type) {
		infer_type_facts(state, expr);
	} else if (expr->eval == eval_uid) {
		infer_icmp_facts(state, expr, UID_RANGE);
	} else if (expr->eval == eval_xtype) {
		infer_xtype_facts(state, expr);
	} else if (expr->eval == eval_not) {
		expr = optimize_not_expr_recursive(state, expr);
	} else if (expr->eval == eval_and) {
		expr = optimize_and_expr_recursive(state, expr);
	} else if (expr->eval == eval_or) {
		expr = optimize_or_expr_recursive(state, expr);
	} else if (expr->eval == eval_comma) {
		expr = optimize_comma_expr_recursive(state, expr);
	} else if (!expr->pure) {
		facts_union(state->facts_when_impure, state->facts_when_impure, &state->facts);
	}

	if (!expr) {
		goto done;
	}

	struct expr *lhs = expr->lhs;
	struct expr *rhs = expr->rhs;
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

	if (expr->always_true) {
		set_facts_impossible(&state->facts_when_false);
	}
	if (expr->always_false) {
		set_facts_impossible(&state->facts_when_true);
	}

	if (state->cmdline->optlevel < 2 || expr == &expr_true || expr == &expr_false) {
		goto done;
	}

	if (facts_impossible(&state->facts_when_true)) {
		if (expr->pure) {
			debug_opt(state, "-O2: data flow: %e --> %e\n", expr, &expr_false);
			free_expr(expr);
			expr = &expr_false;
		} else {
			expr->always_false = true;
			expr->probability = 0.0;
		}
	} else if (facts_impossible(&state->facts_when_false)) {
		if (expr->pure) {
			debug_opt(state, "-O2: data flow: %e --> %e\n", expr, &expr_true);
			free_expr(expr);
			expr = &expr_true;
		} else {
			expr->always_true = true;
			expr->probability = 1.0;
		}
	}

done:
	return expr;
}

/** Swap the children of a binary expression if it would reduce the cost. */
static bool reorder_expr(const struct opt_state *state, struct expr *expr, double swapped_cost) {
	if (swapped_cost < expr->cost) {
		debug_opt(state, "-O3: cost: %e", expr);
		struct expr *lhs = expr->lhs;
		expr->lhs = expr->rhs;
		expr->rhs = lhs;
		debug_opt(state, " <==> %e (~%g --> ~%g)\n", expr, expr->cost, swapped_cost);
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
static bool reorder_expr_recursive(const struct opt_state *state, struct expr *expr) {
	bool ret = false;
	struct expr *lhs = expr->lhs;
	struct expr *rhs = expr->rhs;

	if (lhs) {
		ret |= reorder_expr_recursive(state, lhs);
	}
	if (rhs) {
		ret |= reorder_expr_recursive(state, rhs);
	}

	if (expr->eval == eval_and || expr->eval == eval_or) {
		if (lhs->pure && rhs->pure) {
			double rhs_prob = expr->eval == eval_and ? rhs->probability : 1.0 - rhs->probability;
			double swapped_cost = rhs->cost + rhs_prob*lhs->cost;
			ret |= reorder_expr(state, expr, swapped_cost);
		}
	}

	return ret;
}

int optimize_cmdline(struct cmdline *cmdline) {
	struct opt_facts facts_when_impure;
	set_facts_impossible(&facts_when_impure);

	struct opt_state state = {
		.cmdline = cmdline,
		.facts_when_impure = &facts_when_impure,
	};
	facts_init(&state.facts);

	struct range *depth = state.facts.ranges + DEPTH_RANGE;
	depth->min = cmdline->mindepth;
	depth->max = cmdline->maxdepth;

	int optlevel = cmdline->optlevel;

	cmdline->expr = optimize_expr_recursive(&state, cmdline->expr);
	if (!cmdline->expr) {
		return -1;
	}

	if (optlevel >= 3 && reorder_expr_recursive(&state, cmdline->expr)) {
		// Re-do optimizations to account for the new ordering
		set_facts_impossible(&facts_when_impure);
		cmdline->expr = optimize_expr_recursive(&state, cmdline->expr);
		if (!cmdline->expr) {
			return -1;
		}
	}

	cmdline->expr = ignore_result(&state, cmdline->expr);

	const struct range *depth_when_impure = facts_when_impure.ranges + DEPTH_RANGE;
	long long mindepth = depth_when_impure->min;
	long long maxdepth = depth_when_impure->max;

	if (optlevel >= 2 && mindepth > cmdline->mindepth) {
		if (mindepth > INT_MAX) {
			mindepth = INT_MAX;
		}
		cmdline->mindepth = mindepth;
		debug_opt(&state, "-O2: data flow: mindepth --> %d\n", cmdline->mindepth);
	}

	if (optlevel >= 4 && maxdepth < cmdline->maxdepth) {
		if (maxdepth < INT_MIN) {
			maxdepth = INT_MIN;
		}
		cmdline->maxdepth = maxdepth;
		debug_opt(&state, "-O4: data flow: maxdepth --> %d\n", cmdline->maxdepth);
	}

	return 0;
}
