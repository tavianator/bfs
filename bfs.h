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

#ifndef BFS_H
#define BFS_H

#include "color.h"
#include <stdbool.h>
#include <stddef.h>
#include <time.h>

/**
 * The parsed command line.
 */
typedef struct cmdline cmdline;

/**
 * A command line expression.
 */
typedef struct expression expression;

/**
 * Ephemeral state for evaluating an expression.
 */
typedef struct eval_state eval_state;

/**
 * Expression evaluation function.
 *
 * @param expr
 *         The current expression.
 * @param state
 *         The current evaluation state.
 * @return
 *         The result of the test.
 */
typedef bool eval_fn(const expression *expr, eval_state *state);

struct cmdline {
	/** The array of paths to start from. */
	const char **roots;
	/** The number of root paths. */
	size_t nroots;

	/** Color data. */
	color_table *colors;
	/** -color option. */
	bool color;

	/** -mindepth option. */
	int mindepth;
	/** -maxdepth option. */
	int maxdepth;

	/** bftw() flags. */
	int flags;

	/** The command line expression. */
	expression *expr;

	/** The current time. */
	struct timespec now;
};

/**
 * Possible types of numeric comparison.
 */
typedef enum {
	/** Exactly n. */
	CMP_EXACT,
	/** Less than n. */
	CMP_LESS,
	/** Greater than n. */
	CMP_GREATER,
} cmpflag;

struct expression {
	/** The left hand side of the expression. */
	expression *lhs;
	/** The right hand side of the expression. */
	expression *rhs;
	/** The function that evaluates this expression. */
	eval_fn *eval;
	/** The comparison flag. */
	cmpflag cmp;
	/** Optional integer data for this expression. */
	int idata;
	/** Optional string data for this expression. */
	const char *sdata;
};

/**
 * Parse the command line.
 */
cmdline *parse_cmdline(int argc, char *argv[]);

/**
 * Evaluate the command line.
 */
int eval_cmdline(cmdline *cl);

/**
 * Free the parsed command line.
 */
void free_cmdline(cmdline *cl);

// Predicate evaluation functions
bool eval_true(const expression *expr, eval_state *state);
bool eval_false(const expression *expr, eval_state *state);

bool eval_access(const expression *expr, eval_state *state);

bool eval_amin(const expression *expr, eval_state *state);
bool eval_atime(const expression *expr, eval_state *state);
bool eval_cmin(const expression *expr, eval_state *state);
bool eval_ctime(const expression *expr, eval_state *state);
bool eval_mmin(const expression *expr, eval_state *state);
bool eval_mtime(const expression *expr, eval_state *state);

bool eval_gid(const expression *expr, eval_state *state);
bool eval_uid(const expression *expr, eval_state *state);

bool eval_empty(const expression *expr, eval_state *state);
bool eval_hidden(const expression *expr, eval_state *state);
bool eval_type(const expression *expr, eval_state *state);

bool eval_name(const expression *expr, eval_state *state);
bool eval_path(const expression *expr, eval_state *state);

bool eval_delete(const expression *expr, eval_state *state);
bool eval_nohidden(const expression *expr, eval_state *state);
bool eval_print(const expression *expr, eval_state *state);
bool eval_print0(const expression *expr, eval_state *state);
bool eval_prune(const expression *expr, eval_state *state);
bool eval_quit(const expression *expr, eval_state *state);

// Operator evaluation functions
bool eval_not(const expression *expr, eval_state *state);
bool eval_and(const expression *expr, eval_state *state);
bool eval_or(const expression *expr, eval_state *state);
bool eval_comma(const expression *expr, eval_state *state);

#endif // BFS_H
