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
#include <stdarg.h>
#include <stdio.h>

static char *fake_and_arg = "-a";
static char *fake_or_arg = "-o";

/**
 * Log an optimization.
 */
static void debug_opt(const struct cmdline *cmdline, const char *format, ...) {
	if (!(cmdline->debug & DEBUG_OPT)) {
		return;
	}

	CFILE *cerr = cmdline->cerr;

	va_list args;
	va_start(args, format);

	for (const char *i = format; *i != '\0'; ++i) {
		if (*i == '%') {
			switch (*++i) {
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

/**
 * Extract a child expression, freeing the outer expression.
 */
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

static struct expr *optimize_not_expr(const struct cmdline *cmdline, struct expr *expr);
static struct expr *optimize_and_expr(const struct cmdline *cmdline, struct expr *expr);
static struct expr *optimize_or_expr(const struct cmdline *cmdline, struct expr *expr);

/**
 * Apply De Morgan's laws.
 */
static struct expr *de_morgan(const struct cmdline *cmdline, struct expr *expr, char **argv) {
	debug_opt(cmdline, "-O1: De Morgan's laws: %e ", expr);

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

	debug_opt(cmdline, "<==> %e\n", parent);

	if (expr->lhs->eval == eval_not) {
		expr->lhs = optimize_not_expr(cmdline, expr->lhs);
	}
	if (expr->rhs->eval == eval_not) {
		expr->rhs = optimize_not_expr(cmdline, expr->rhs);
	}
	if (!expr->lhs || !expr->rhs) {
		free_expr(parent);
		return NULL;
	}

	if (expr->eval == eval_and) {
		expr = optimize_and_expr(cmdline, expr);
	} else {
		expr = optimize_or_expr(cmdline, expr);
	}
	if (!expr) {
		if (has_parent) {
			parent->rhs = NULL;
			free_expr(parent);
		}
		return NULL;
	}

	if (has_parent) {
		parent = optimize_not_expr(cmdline, parent);
	}
	return parent;
}

/**
 * Optimize a negation.
 */
static struct expr *optimize_not_expr(const struct cmdline *cmdline, struct expr *expr) {
	assert(expr->eval == eval_not && !expr->lhs && expr->rhs);

	struct expr *rhs = expr->rhs;

	int optlevel = cmdline->optlevel;
	if (optlevel >= 1) {
		if (rhs == &expr_true) {
			debug_opt(cmdline, "-O1: constant propagation: %e <==> %e\n", expr, &expr_false);
			free_expr(expr);
			return &expr_false;
		} else if (rhs == &expr_false) {
			debug_opt(cmdline, "-O1: constant propagation: %e <==> %e\n", expr, &expr_true);
			free_expr(expr);
			return &expr_true;
		} else if (rhs->eval == eval_not) {
			debug_opt(cmdline, "-O1: double negation: %e <==> %e\n", expr, rhs->rhs);
			return extract_child_expr(expr, &rhs->rhs);
		} else if (expr_never_returns(rhs)) {
			debug_opt(cmdline, "-O1: reachability: %e <==> %e\n", expr, rhs);
			return extract_child_expr(expr, &expr->rhs);
		} else if ((rhs->eval == eval_and || rhs->eval == eval_or)
			   && (rhs->lhs->eval == eval_not || rhs->rhs->eval == eval_not)) {
			return de_morgan(cmdline, expr, expr->argv);
		}
	}

	expr->pure = rhs->pure;
	expr->always_true = rhs->always_false;
	expr->always_false = rhs->always_true;
	expr->cost = rhs->cost;
	expr->probability = 1.0 - rhs->probability;

	return expr;
}

/**
 * Optimize a conjunction.
 */
static struct expr *optimize_and_expr(const struct cmdline *cmdline, struct expr *expr) {
	assert(expr->eval == eval_and && expr->lhs && expr->rhs);

	struct expr *lhs = expr->lhs;
	struct expr *rhs = expr->rhs;

	int optlevel = cmdline->optlevel;
	if (optlevel >= 1) {
		if (lhs == &expr_true) {
			debug_opt(cmdline, "-O1: conjunction elimination: %e <==> %e\n", expr, rhs);
			return extract_child_expr(expr, &expr->rhs);
		} else if (rhs == &expr_true) {
			debug_opt(cmdline, "-O1: conjunction elimination: %e <==> %e\n", expr, lhs);
			return extract_child_expr(expr, &expr->lhs);
		} else if (lhs->always_false) {
			debug_opt(cmdline, "-O1: short-circuit: %e <==> %e\n", expr, lhs);
			return extract_child_expr(expr, &expr->lhs);
		} else if (optlevel >= 2 && lhs->pure && rhs == &expr_false) {
			debug_opt(cmdline, "-O2: purity: %e <==> %e\n", expr, rhs);
			return extract_child_expr(expr, &expr->rhs);
		} else if (lhs->eval == eval_not && rhs->eval == eval_not) {
			return de_morgan(cmdline, expr, expr->lhs->argv);
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
			debug_opt(cmdline, "-O3: cost: %e", expr);
			expr->lhs = rhs;
			expr->rhs = lhs;
			debug_opt(cmdline, " <==> %e (~%g --> ~%g)\n", expr, expr->cost, swapped_cost);
			expr->cost = swapped_cost;
		}
	}

	return expr;
}

/**
 * Optimize a disjunction.
 */
static struct expr *optimize_or_expr(const struct cmdline *cmdline, struct expr *expr) {
	assert(expr->eval == eval_or && expr->lhs && expr->rhs);

	struct expr *lhs = expr->lhs;
	struct expr *rhs = expr->rhs;

	int optlevel = cmdline->optlevel;
	if (optlevel >= 1) {
		if (lhs->always_true) {
			debug_opt(cmdline, "-O1: short-circuit: %e <==> %e\n", expr, lhs);
			return extract_child_expr(expr, &expr->lhs);
		} else if (lhs == &expr_false) {
			debug_opt(cmdline, "-O1: disjunctive syllogism: %e <==> %e\n", expr, rhs);
			return extract_child_expr(expr, &expr->rhs);
		} else if (rhs == &expr_false) {
			debug_opt(cmdline, "-O1: disjunctive syllogism: %e <==> %e\n", expr, lhs);
			return extract_child_expr(expr, &expr->lhs);
		} else if (optlevel >= 2 && lhs->pure && rhs == &expr_true) {
			debug_opt(cmdline, "-O2: purity: %e <==> %e\n", expr, rhs);
			return extract_child_expr(expr, &expr->rhs);
		} else if (lhs->eval == eval_not && rhs->eval == eval_not) {
			return de_morgan(cmdline, expr, expr->lhs->argv);
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
			debug_opt(cmdline, "-O3: cost: %e", expr);
			expr->lhs = rhs;
			expr->rhs = lhs;
			debug_opt(cmdline, " <==> %e (~%g --> ~%g)\n", expr, expr->cost, swapped_cost);
			expr->cost = swapped_cost;
		}
	}

	return expr;
}

/**
 * Optimize an expression in an ignored-result context.
 */
static struct expr *ignore_result(const struct cmdline *cmdline, struct expr *expr) {
	int optlevel = cmdline->optlevel;

	if (optlevel >= 1) {
		while (true) {
			if (expr->eval == eval_not) {
				debug_opt(cmdline, "-O1: ignored result: %e --> %e\n", expr, expr->rhs);
				expr = extract_child_expr(expr, &expr->rhs);
			} else if (optlevel >= 2
			           && (expr->eval == eval_and || expr->eval == eval_or || expr->eval == eval_comma)
			           && expr->rhs->pure) {
				debug_opt(cmdline, "-O2: ignored result: %e --> %e\n", expr, expr->lhs);
				expr = extract_child_expr(expr, &expr->lhs);
			} else {
				break;
			}
		}
	}

	return expr;
}

/**
 * Optimize a comma expression.
 */
static struct expr *optimize_comma_expr(const struct cmdline *cmdline, struct expr *expr) {
	struct expr *lhs = expr->lhs;
	struct expr *rhs = expr->rhs;

	int optlevel = cmdline->optlevel;
	if (optlevel >= 1) {
		lhs = expr->lhs = ignore_result(cmdline, lhs);

		if (expr_never_returns(lhs)) {
			debug_opt(cmdline, "-O1: reachability: %e <==> %e\n", expr, lhs);
			return extract_child_expr(expr, &expr->lhs);
		}

		if (optlevel >= 2 && lhs->pure) {
			debug_opt(cmdline, "-O2: purity: %e <==> %e\n", expr, rhs);
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

/**
 * Optimize an expression.
 */
static struct expr *optimize_expr(const struct cmdline *cmdline, struct expr *expr) {
	if (expr->lhs) {
		expr->lhs = optimize_expr(cmdline, expr->lhs);
		if (!expr->lhs) {
			goto fail;
		}
	}

	if (expr->rhs) {
		expr->rhs = optimize_expr(cmdline, expr->rhs);
		if (!expr->rhs) {
			goto fail;
		}
	}

	if (expr->eval == eval_not) {
		return optimize_not_expr(cmdline, expr);
	} else if (expr->eval == eval_and) {
		return optimize_and_expr(cmdline, expr);
	} else if (expr->eval == eval_or) {
		return optimize_or_expr(cmdline, expr);
	} else if (expr->eval == eval_comma) {
		return optimize_comma_expr(cmdline, expr);
	} else {
		return expr;
	}

fail:
	free_expr(expr);
	return NULL;
}

/**
 * Apply top-level optimizations.
 */
static struct expr *optimize_whole_expr(const struct cmdline *cmdline, struct expr *expr) {
	expr = optimize_expr(cmdline, expr);
	if (!expr) {
		return NULL;
	}

	expr = ignore_result(cmdline, expr);

	if (cmdline->optlevel >= 4 && expr->pure && expr != &expr_false) {
		debug_opt(cmdline, "-O4: top-level purity: %e --> %e\n", expr, &expr_false);
		free_expr(expr);
		expr = &expr_false;
	}

	return expr;
}

int optimize_cmdline(struct cmdline *cmdline) {
	cmdline->expr = optimize_whole_expr(cmdline, cmdline->expr);
	if (cmdline->expr) {
		return 0;
	} else {
		return -1;
	}
}
