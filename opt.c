/****************************************************************************
 * bfs                                                                      *
 * Copyright (C) 2017 Tavian Barnes <tavianator@tavianator.com>             *
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

/**
 * Data flow facts about an evaluation point.
 */
struct opt_facts {
	/** Minimum possible depth at this point. */
	int mindepth;
	/** Maximum possible depth at this point. */
	int maxdepth;

	/** Bitmask of possible file types at this point. */
	enum bftw_typeflag types;
};

/** Compute the union of two fact sets. */
static void facts_union(struct opt_facts *result, const struct opt_facts *lhs, const struct opt_facts *rhs) {
	if (lhs->mindepth < rhs->mindepth) {
		result->mindepth = lhs->mindepth;
	} else {
		result->mindepth = rhs->mindepth;
	}

	if (lhs->maxdepth > rhs->maxdepth) {
		result->maxdepth = lhs->maxdepth;
	} else {
		result->maxdepth = rhs->maxdepth;
	}

	result->types = lhs->types | rhs->types;
}

/** Determine whether a fact set is impossible. */
static bool facts_impossible(const struct opt_facts *facts) {
	return facts->mindepth > facts->maxdepth || !facts->types;
}

/** Set some facts to be impossible. */
static void set_facts_impossible(struct opt_facts *facts) {
	facts->mindepth = INT_MAX;
	facts->maxdepth = -1;
	facts->types = 0;
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
				cfprintf(cerr, "%{ylw}%g%{rs}", va_arg(args, double));
				break;
			}
		} else {
			fputc(*i, stderr);
		}
	}

	va_end(args);
}

/** Update the inferred mindepth. */
static void update_mindepth(struct opt_facts *facts, long long mindepth) {
	if (mindepth > facts->mindepth) {
		if (mindepth > INT_MAX) {
			facts->maxdepth = -1;
		} else {
			facts->mindepth = mindepth;
		}
	}
}

/** Update the inferred maxdepth. */
static void update_maxdepth(struct opt_facts *facts, long long maxdepth) {
	if (maxdepth < facts->maxdepth) {
		facts->maxdepth = maxdepth;
	}
}

/** Infer data flow facts about a -depth N expression. */
static void infer_depth_facts(struct opt_state *state, const struct expr *expr) {
	switch (expr->cmp_flag) {
	case CMP_EXACT:
		update_mindepth(&state->facts_when_true, expr->idata);
		update_maxdepth(&state->facts_when_true, expr->idata);
		break;

	case CMP_LESS:
		update_maxdepth(&state->facts_when_true, expr->idata - 1);
		update_mindepth(&state->facts_when_false, expr->idata);
		break;

	case CMP_GREATER:
		if (expr->idata == LLONG_MAX) {
			// Avoid overflow
			state->facts_when_true.maxdepth = -1;
		} else {
			update_mindepth(&state->facts_when_true, expr->idata + 1);
		}
		update_maxdepth(&state->facts_when_false, expr->idata);
		break;
	}
}

/** Infer data flow facts about a -type expression. */
static void infer_type_facts(struct opt_state *state, const struct expr *expr) {
	state->facts_when_true.types &= expr->idata;
	state->facts_when_false.types &= ~expr->idata;
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

	if (optlevel >= 3 && lhs->pure && rhs->pure) {
		double swapped_cost = rhs->cost + rhs->probability*lhs->cost;
		if (swapped_cost < expr->cost) {
			debug_opt(state, "-O3: cost: %e", expr);
			expr->lhs = rhs;
			expr->rhs = lhs;
			debug_opt(state, " <==> %e (~%g --> ~%g)\n", expr, expr->cost, swapped_cost);
			expr->cost = swapped_cost;
		}
	}

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

	if (optlevel >= 3 && lhs->pure && rhs->pure) {
		double swapped_cost = rhs->cost + (1 - rhs->probability)*lhs->cost;
		if (swapped_cost < expr->cost) {
			debug_opt(state, "-O3: cost: %e", expr);
			expr->lhs = rhs;
			expr->rhs = lhs;
			debug_opt(state, " <==> %e (~%g --> ~%g)\n", expr, expr->cost, swapped_cost);
			expr->cost = swapped_cost;
		}
	}

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
		}

		if (optlevel >= 2 && lhs->pure) {
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

static struct expr *optimize_expr_recursive(struct opt_state *state, struct expr *expr) {
	state->facts_when_true = state->facts;
	state->facts_when_false = state->facts;

	if (expr->eval == eval_depth) {
		infer_depth_facts(state, expr);
	} else if (expr->eval == eval_type) {
		infer_type_facts(state, expr);
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

int optimize_cmdline(struct cmdline *cmdline) {
	struct opt_facts facts_when_impure;
	set_facts_impossible(&facts_when_impure);

	struct opt_state state = {
		.cmdline = cmdline,
		.facts = {
			.mindepth = cmdline->mindepth,
			.maxdepth = cmdline->maxdepth,
			.types = ~0,
		},
		.facts_when_impure = &facts_when_impure,
	};

	cmdline->expr = optimize_expr_recursive(&state, cmdline->expr);
	if (!cmdline->expr) {
		return -1;
	}

	cmdline->expr = ignore_result(&state, cmdline->expr);

	int optlevel = cmdline->optlevel;

	if (optlevel >= 2 && facts_when_impure.mindepth > cmdline->mindepth) {
		debug_opt(&state, "-O2: data flow: mindepth --> %d\n", facts_when_impure.mindepth);
		cmdline->mindepth = facts_when_impure.mindepth;
	}
	if (optlevel >= 4 && facts_when_impure.maxdepth < cmdline->maxdepth) {
		debug_opt(&state, "-O4: data flow: maxdepth --> %d\n", facts_when_impure.maxdepth);
		cmdline->maxdepth = facts_when_impure.maxdepth;
	}

	return 0;
}
