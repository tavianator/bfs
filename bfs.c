/*********************************************************************
 * bfs                                                               *
 * Copyright (C) 2015 Tavian Barnes <tavianator@tavianator.com>      *
 *                                                                   *
 * This program is free software. It comes without any warranty, to  *
 * the extent permitted by applicable law. You can redistribute it   *
 * and/or modify it under the terms of the Do What The Fuck You Want *
 * To Public License, Version 2, as published by Sam Hocevar. See    *
 * the COPYING file or http://www.wtfpl.net/ for more details.       *
 *********************************************************************/

#include "bftw.h"
#include "color.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <unistd.h>

/**
 * A command line expression.
 */
typedef struct expression expression;

/**
 * The parsed command line.
 */
typedef struct cmdline cmdline;

/**
 * Expression evaluation function.
 *
 * @param fpath
 *         The path to the encountered file.
 * @param ftwbuf
 *         Additional data about the current file.
 * @param cmdline
 *         The parsed command line.
 * @param expr
 *         The current expression.
 * @param ret
 *         A pointer to the bftw() callback return value.
 * @return
 *         The result of the test.
 */
typedef bool eval_fn(const char *fpath, const struct BFTW *ftwbuf, const cmdline *cl, const expression *expr, int *ret);

struct expression {
	/** The left hand side of the expression. */
	expression *lhs;
	/** The right hand side of the expression. */
	expression *rhs;
	/** The function that evaluates this expression. */
	eval_fn *eval;
	/** Optional data for this expression. */
	int data;
};

/**
 * Create a new expression.
 */
static expression *new_expression(expression *lhs, expression *rhs, eval_fn *eval, int data) {
	expression *expr = malloc(sizeof(expression));
	if (!expr) {
		perror("malloc()");
		return NULL;
	}

	expr->lhs = lhs;
	expr->rhs = rhs;
	expr->eval = eval;
	expr->data = data;
	return expr;
}

/**
 * Free an expression.
 */
static void free_expression(expression *expr) {
	if (expr) {
		free_expression(expr->lhs);
		free_expression(expr->rhs);
		free(expr);
	}
}

struct cmdline {
	/** The array of paths to start from. */
	const char **roots;
	/** The number of root paths. */
	size_t nroots;

	/** Color data. */
	color_table *colors;
	/** -color option. */
	bool color;

	/** bftw() flags. */
	int flags;

	/** The command line expression. */
	expression *expr;
};

/**
 * Free the parsed command line.
 */
static void free_cmdline(cmdline *cl) {
	if (cl) {
		free_expression(cl->expr);
		free(cl->roots);
		free(cl);
	}
}

/**
 * Add a root path to the cmdline.
 */
static bool cmdline_add_root(cmdline *cl, const char *root) {
	size_t i = cl->nroots++;
	const char **roots = realloc(cl->roots, cl->nroots*sizeof(const char *));
	if (!roots) {
		perror("realloc()");
		return false;
	}

	roots[i] = root;
	cl->roots = roots;
	return true;
}

/**
 * Ephemeral state for parsing the command line.
 */
typedef struct {
	/** The command line being parsed. */
	cmdline *cl;
	/** The command line arguments. */
	char **argv;
	/** Current argument index. */
	int i;

	/** Whether a -print action is implied. */
	bool implicit_print;
} parser_state;

/**
 * Parse the expression specified on the command line.
 */
static expression *parse_expression(parser_state *state);

/**
 * While parsing an expression, skip any paths and add them to the cmdline.
 */
static const char *skip_paths(parser_state *state) {
	while (true) {
		const char *arg = state->argv[state->i];
		if (!arg
		    || arg[0] == '-'
		    || strcmp(arg, "(") == 0
		    || strcmp(arg, ")") == 0
		    || strcmp(arg, "!") == 0
		    || strcmp(arg, ",") == 0) {
			return arg;
		}

		if (!cmdline_add_root(state->cl, arg)) {
			return NULL;
		}

		++state->i;
	}
}

/**
 * Always true.
 */
static bool eval_true(const char *fpath, const struct BFTW *ftwbuf, const cmdline *cl, const expression *expr, int *ret) {
	return true;
}

/**
 * -print action.
 */
static bool eval_print(const char *fpath, const struct BFTW *ftwbuf, const cmdline *cl, const expression *expr, int *ret) {
	pretty_print(cl->colors, fpath, ftwbuf->statbuf);
	return true;
}

/**
 * -prune action.
 */
static bool eval_prune(const char *fpath, const struct BFTW *ftwbuf, const cmdline *cl, const expression *expr, int *ret) {
	*ret = BFTW_SKIP_SUBTREE;
	return true;
}

/**
 * -hidden test.
 */
static bool eval_hidden(const char *fpath, const struct BFTW *ftwbuf, const cmdline *cl, const expression *expr, int *ret) {
	return ftwbuf->base > 0 && fpath[ftwbuf->base] == '.';
}

/**
 * -nohidden action.
 */
static bool eval_nohidden(const char *fpath, const struct BFTW *ftwbuf, const cmdline *cl, const expression *expr, int *ret) {
	return !eval_hidden(fpath, ftwbuf, cl, expr, ret)
		|| eval_prune(fpath, ftwbuf, cl, expr, ret);
}

/**
 * -type test.
 */
static bool eval_type(const char *fpath, const struct BFTW *ftwbuf, const cmdline *cl, const expression *expr, int *ret) {
	return ftwbuf->typeflag == expr->data;
}

/**
 * Parse -type [bcdpfls].
 */
static expression *parse_type(parser_state *state) {
	const char *arg = state->argv[state->i];
	if (!arg) {
		fputs("-type needs a value.\n", stderr);
		return NULL;
	}

	int typeflag = BFTW_UNKNOWN;

	switch (arg[0]) {
	case 'b':
		typeflag = BFTW_BLK;
		break;
	case 'c':
		typeflag = BFTW_CHR;
		break;
	case 'd':
		typeflag = BFTW_DIR;
		break;
	case 'p':
		typeflag = BFTW_FIFO;
		break;
	case 'f':
		typeflag = BFTW_REG;
		break;
	case 'l':
		typeflag = BFTW_LNK;
		break;
	case 's':
		typeflag = BFTW_SOCK;
		break;
	}

	if (typeflag == BFTW_UNKNOWN || arg[1] != '\0') {
		fprintf(stderr, "Unknown type flag '%s'.\n", arg);
		return NULL;
	}

	++state->i;

	return new_expression(NULL, NULL, eval_type, typeflag);
}

/**
 * LITERAL : OPTION
 *         | TEST
 *         | ACTION
 */
static expression *parse_literal(parser_state *state) {
	// Paths are already skipped at this point
	const char *arg = state->argv[state->i++];

	if (strcmp(arg, "-color") == 0) {
		state->cl->color = true;
		return new_expression(NULL, NULL, eval_true, 0);
	} else if (strcmp(arg, "-nocolor") == 0) {
		state->cl->color = false;
		return new_expression(NULL, NULL, eval_true, 0);
	} else if (strcmp(arg, "-hidden") == 0) {
		return new_expression(NULL, NULL, eval_hidden, 0);
	} else if (strcmp(arg, "-nohidden") == 0) {
		return new_expression(NULL, NULL, eval_nohidden, 0);
	} else if (strcmp(arg, "-print") == 0) {
		state->implicit_print = false;
		return new_expression(NULL, NULL, eval_print, 0);
	} else if (strcmp(arg, "-prune") == 0) {
		return new_expression(NULL, NULL, eval_prune, 0);
	} else if (strcmp(arg, "-type") == 0) {
		return parse_type(state);
	} else {
		fprintf(stderr, "Unknown argument '%s'.\n", arg);
		return NULL;
	}
}

/**
 * Evaluate a negation.
 */
static bool eval_not(const char *fpath, const struct BFTW *ftwbuf, const cmdline *cl, const expression *expr, int *ret) {
	return !expr->rhs->eval(fpath, ftwbuf, cl, expr->rhs, ret);
}

/**
 * FACTOR : "(" EXPR ")"
 *        | "!" FACTOR | "-not" FACTOR
 *        | LITERAL
 */
static expression *parse_factor(parser_state *state) {
	const char *arg = skip_paths(state);
	if (!arg) {
		fputs("Expression terminated prematurely.\n", stderr);
		return NULL;
	}

	if (strcmp(arg, "(") == 0) {
		++state->i;
		expression *expr = parse_expression(state);
		if (!expr) {
			return NULL;
		}

		arg = skip_paths(state);
		if (!arg || strcmp(arg, ")") != 0) {
			fputs("Expected a ')'.\n", stderr);
			free_expression(expr);
			return NULL;
		}
		++state->i;

		return expr;
	} else if (strcmp(arg, "!") == 0 || strcmp(arg, "-not") == 0) {
		++state->i;

		expression *factor = parse_factor(state);
		if (!factor) {
			return NULL;
		}

		return new_expression(NULL, factor, eval_not, 0);
	} else {
		return parse_literal(state);
	}
}

/**
 * Evaluate a conjunction.
 */
static bool eval_and(const char *fpath, const struct BFTW *ftwbuf, const cmdline *cl, const expression *expr, int *ret) {
	return expr->lhs->eval(fpath, ftwbuf, cl, expr->lhs, ret)
		&& expr->rhs->eval(fpath, ftwbuf, cl, expr->rhs, ret);
}

/**
 * TERM : FACTOR
 *      | TERM FACTOR
 *      | TERM "-a" FACTOR
 *      | TERM "-and" FACTOR
 */
static expression *parse_term(parser_state *state) {
	expression *term = parse_factor(state);

	while (term) {
		const char *arg = skip_paths(state);
		if (!arg) {
			break;
		}

		if (strcmp(arg, "-a") == 0 || strcmp(arg, "-and") == 0) {
			++state->i;
		}

		expression *lhs = term;
		expression *rhs = parse_factor(state);
		if (!rhs) {
			return NULL;
		}

		term = new_expression(lhs, rhs, eval_and, 0);
	}

	return term;
}

/**
 * Evaluate a disjunction.
 */
static bool eval_or(const char *fpath, const struct BFTW *ftwbuf, const cmdline *cl, const expression *expr, int *ret) {
	return expr->lhs->eval(fpath, ftwbuf, cl, expr->lhs, ret)
		|| expr->rhs->eval(fpath, ftwbuf, cl, expr->rhs, ret);
}

/**
 * CLAUSE : TERM
 *        | CLAUSE "-o" TERM
 *        | CLAUSE "-or" TERM
 */
static expression *parse_clause(parser_state *state) {
	expression *clause = parse_term(state);

	while (clause) {
		const char *arg = skip_paths(state);
		if (!arg) {
			break;
		}

		if (strcmp(arg, "-o") != 0 && strcmp(arg, "-or") != 0) {
			break;
		}

		++state->i;

		expression *lhs = clause;
		expression *rhs = parse_term(state);
		if (!rhs) {
			return NULL;
		}

		clause = new_expression(lhs, rhs, eval_or, 0);
	}

	return clause;
}

/**
 * Evaluate the comma operator.
 */
static bool eval_comma(const char *fpath, const struct BFTW *ftwbuf, const cmdline *cl, const expression *expr, int *ret) {
	expr->lhs->eval(fpath, ftwbuf, cl, expr->lhs, ret);
	return expr->rhs->eval(fpath, ftwbuf, cl, expr->rhs, ret);
}

/**
 * EXPR : CLAUSE
 *      | EXPR "," CLAUSE
 */
static expression *parse_expression(parser_state *state) {
	expression *expr = parse_clause(state);

	while (expr) {
		const char *arg = skip_paths(state);
		if (!arg) {
			break;
		}

		if (strcmp(arg, ",") != 0) {
			break;
		}

		++state->i;

		expression *lhs = expr;
		expression *rhs = parse_clause(state);
		if (!rhs) {
			return NULL;
		}

		expr = new_expression(lhs, rhs, eval_comma, 0);
	}

	return expr;
}

/**
 * Parse the command line.
 */
static cmdline *parse_cmdline(int argc, char *argv[]) {
	cmdline *cl = malloc(sizeof(cmdline));
	if (!cl) {
		goto fail;
	}

	cl->roots = NULL;
	cl->nroots = 0;
	cl->colors = NULL;
	cl->color = isatty(STDOUT_FILENO);
	cl->flags = BFTW_RECOVER;

	parser_state state = {
		.cl = cl,
		.argv = argv,
		.i = 1,
		.implicit_print = true,
	};

	if (skip_paths(&state)) {
		cl->expr = parse_expression(&state);
		if (!cl->expr) {
			goto fail;
		}
	}

	if (state.i < argc) {
		fprintf(stderr, "Unexpected argument '%s'.\n", argv[state.i]);
		goto fail;
	}

	if (state.implicit_print) {
		expression *print = new_expression(NULL, NULL, eval_print, 0);
		if (!print) {
			goto fail;
		}

		if (cl->expr) {
			expression *expr = new_expression(cl->expr, print, eval_and, 0);
			if (!expr) {
				free_expression(print);
				goto fail;
			}

			cl->expr = expr;
		} else {
			cl->expr = print;
		}
	}

	if (cl->nroots == 0) {
		if (!cmdline_add_root(cl, ".")) {
			goto fail;
		}
	}

	if (cl->color) {
		cl->colors = parse_colors(getenv("LS_COLORS"));
		cl->flags |= BFTW_STAT;
	}

	return cl;

fail:
	free_cmdline(cl);
	return NULL;
}

/**
 * Infer the number of open file descriptors we're allowed to have.
 */
static int infer_nopenfd() {
	int ret = 4096;

	struct rlimit rl;
	if (getrlimit(RLIMIT_NOFILE, &rl) == 0) {
		if (rl.rlim_cur != RLIM_INFINITY) {
			ret = rl.rlim_cur;
		}
	}

	// Account for std{in,out,err}
	if (ret > 3) {
		ret -= 3;
	}

	return ret;
}

/**
 * bftw() callback.
 */
static int cmdline_callback(const char *fpath, const struct BFTW *ftwbuf, void *ptr) {
	const cmdline *cl = ptr;

	if (ftwbuf->typeflag == BFTW_ERROR) {
		print_error(cl->colors, fpath, ftwbuf);
		return BFTW_SKIP_SUBTREE;
	}

	int ret = BFTW_CONTINUE;
	cl->expr->eval(fpath, ftwbuf, cl, cl->expr, &ret);
	return ret;
}

/**
 * Evaluate the command line.
 */
static int cmdline_eval(cmdline *cl) {
	int ret = 0;
	int nopenfd = infer_nopenfd();

	for (size_t i = 0; i < cl->nroots; ++i) {
		if (bftw(cl->roots[i], cmdline_callback, nopenfd, cl->flags, cl) != 0) {
			ret = -1;
			perror("bftw()");
		}
	}

	return ret;
}

int main(int argc, char *argv[]) {
	int ret = EXIT_FAILURE;

	cmdline *cl = parse_cmdline(argc, argv);
	if (cl) {
		if (cmdline_eval(cl) == 0) {
			ret = EXIT_SUCCESS;
		}
	}

	free_cmdline(cl);
	return ret;
}
