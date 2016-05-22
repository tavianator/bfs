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
#include <sys/types.h>
#include <time.h>

#ifndef BFS_VERSION
#	define BFS_VERSION "0.79"
#endif

#ifndef BFS_HOMEPAGE
#	define BFS_HOMEPAGE "https://github.com/tavianator/bfs"
#endif

/**
 * A command line expression.
 */
struct expr;

/**
 * Ephemeral state for evaluating an expression.
 */
struct eval_state;

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
typedef bool eval_fn(const struct expr *expr, struct eval_state *state);

/**
 * Various debugging flags.
 */
enum debugflags {
	/** Trace all stat() calls. */
	DEBUG_STAT = 1 << 0,
	/** Print the parse tree. */
	DEBUG_TREE = 1 << 1,
};

/**
 * The parsed command line.
 */
struct cmdline {
	/** The array of paths to start from. */
	const char **roots;
	/** The number of root paths. */
	size_t nroots;

	/** Color data. */
	struct colors *colors;
	/** Colors to use for stdout. */
	const struct colors *stdout_colors;
	/** Colors to use for stderr. */
	const struct colors *stderr_colors;

	/** -mindepth option. */
	int mindepth;
	/** -maxdepth option. */
	int maxdepth;

	/** bftw() flags. */
	enum bftw_flags flags;

	/** Optimization level. */
	int optlevel;
	/** Debugging flags. */
	enum debugflags debug;

	/** The command line expression. */
	struct expr *expr;
};

/**
 * Possible types of numeric comparison.
 */
enum cmpflag {
	/** Exactly n. */
	CMP_EXACT,
	/** Less than n. */
	CMP_LESS,
	/** Greater than n. */
	CMP_GREATER,
};

/**
 * Available struct stat time fields.
 */
enum timefield {
	/** Access time. */
	ATIME,
	/** Status change time. */
	CTIME,
	/** Modification time. */
	MTIME,
};

/**
 * Possible time units.
 */
enum timeunit {
	/** Minutes. */
	MINUTES,
	/** Days. */
	DAYS,
};

/**
 * Possible file size units.
 */
enum sizeunit {
	/** 512-byte blocks. */
	SIZE_BLOCKS,
	/** Single bytes. */
	SIZE_BYTES,
	/** Two-byte words. */
	SIZE_WORDS,
	/** Kibibytes. */
	SIZE_KB,
	/** Mebibytes. */
	SIZE_MB,
	/** Gibibytes. */
	SIZE_GB,
};

/**
 * Flags for the -exec actions.
 */
enum execflags {
	/** Prompt the user before executing (-ok, -okdir). */
	EXEC_CONFIRM = 1 << 0,
	/** Run the command in the file's parent directory (-execdir, -okdir). */
	EXEC_CHDIR   = 1 << 1,
	/** Pass multiple files at once to the command (-exec ... {} +). */
	EXEC_MULTI   = 1 << 2,
};

struct expr {
	/** The function that evaluates this expression. */
	eval_fn *eval;

	/** The left hand side of the expression. */
	struct expr *lhs;
	/** The right hand side of the expression. */
	struct expr *rhs;

	/** Whether this expression has no side effects. */
	bool pure;

	/** The number of command line arguments for this expression. */
	size_t argc;
	/** The command line arguments comprising this expression. */
	char **argv;

	/** The optional comparison flag. */
	enum cmpflag cmpflag;

	/** The optional reference time. */
	struct timespec reftime;
	/** The optional time field. */
	enum timefield timefield;
	/** The optional time unit. */
	enum timeunit timeunit;

	/** The optional size unit. */
	enum sizeunit sizeunit;

	/** Optional device number for a target file. */
	dev_t dev;
	/** Optional inode number for a target file. */
	ino_t ino;

	/** Optional -exec flags. */
	enum execflags execflags;

	/** Optional integer data for this expression. */
	long long idata;

	/** Optional string data for this expression. */
	const char *sdata;
};

/**
 * Parse the command line.
 */
struct cmdline *parse_cmdline(int argc, char *argv[]);

/**
 * Evaluate the command line.
 */
int eval_cmdline(const struct cmdline *cmdline);

/**
 * Free the parsed command line.
 */
void free_cmdline(struct cmdline *cmdline);

// Predicate evaluation functions
bool eval_true(const struct expr *expr, struct eval_state *state);
bool eval_false(const struct expr *expr, struct eval_state *state);

bool eval_access(const struct expr *expr, struct eval_state *state);

bool eval_acmtime(const struct expr *expr, struct eval_state *state);
bool eval_acnewer(const struct expr *expr, struct eval_state *state);
bool eval_used(const struct expr *expr, struct eval_state *state);

bool eval_gid(const struct expr *expr, struct eval_state *state);
bool eval_uid(const struct expr *expr, struct eval_state *state);

bool eval_empty(const struct expr *expr, struct eval_state *state);
bool eval_hidden(const struct expr *expr, struct eval_state *state);
bool eval_inum(const struct expr *expr, struct eval_state *state);
bool eval_links(const struct expr *expr, struct eval_state *state);
bool eval_samefile(const struct expr *expr, struct eval_state *state);
bool eval_size(const struct expr *expr, struct eval_state *state);
bool eval_type(const struct expr *expr, struct eval_state *state);
bool eval_xtype(const struct expr *expr, struct eval_state *state);

bool eval_lname(const struct expr *expr, struct eval_state *state);
bool eval_name(const struct expr *expr, struct eval_state *state);
bool eval_path(const struct expr *expr, struct eval_state *state);

bool eval_delete(const struct expr *expr, struct eval_state *state);
bool eval_exec(const struct expr *expr, struct eval_state *state);
bool eval_nohidden(const struct expr *expr, struct eval_state *state);
bool eval_print(const struct expr *expr, struct eval_state *state);
bool eval_print0(const struct expr *expr, struct eval_state *state);
bool eval_prune(const struct expr *expr, struct eval_state *state);
bool eval_quit(const struct expr *expr, struct eval_state *state);

// Operator evaluation functions
bool eval_not(const struct expr *expr, struct eval_state *state);
bool eval_and(const struct expr *expr, struct eval_state *state);
bool eval_or(const struct expr *expr, struct eval_state *state);
bool eval_comma(const struct expr *expr, struct eval_state *state);

#endif // BFS_H
