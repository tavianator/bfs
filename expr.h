/****************************************************************************
 * bfs                                                                      *
 * Copyright (C) 2015-2017 Tavian Barnes <tavianator@tavianator.com>        *
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

#ifndef BFS_EXPR_H
#define BFS_EXPR_H

#include "color.h"
#include "exec.h"
#include "printf.h"
#include "stat.h"
#include <regex.h>
#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>
#include <time.h>

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
 * Possible types of numeric comparison.
 */
enum cmp_flag {
	/** Exactly n. */
	CMP_EXACT,
	/** Less than n. */
	CMP_LESS,
	/** Greater than n. */
	CMP_GREATER,
};

/**
 * Possible types of mode comparison.
 */
enum mode_cmp {
	/** Mode is an exact match (MODE). */
	MODE_EXACT,
	/** Mode has all these bits (-MODE). */
	MODE_ALL,
	/** Mode has any of these bits (/MODE). */
	MODE_ANY,
};

/**
 * Possible time units.
 */
enum time_unit {
	/** Minutes. */
	MINUTES,
	/** Days. */
	DAYS,
};

/**
 * Possible file size units.
 */
enum size_unit {
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
	/** Tebibytes. */
	SIZE_TB,
	/** Pebibytes. */
	SIZE_PB,
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
	/** Whether this expression always evaluates to true. */
	bool always_true;
	/** Whether this expression always evaluates to false. */
	bool always_false;

	/** Estimated cost. */
	double cost;
	/** Estimated probability of success. */
	double probability;
	/** Number of times this predicate was executed. */
	size_t evaluations;
	/** Number of times this predicate succeeded. */
	size_t successes;
	/** Total time spent running this predicate. */
	struct timespec elapsed;

	/** The number of command line arguments for this expression. */
	size_t argc;
	/** The command line arguments comprising this expression. */
	char **argv;

	/** The optional comparison flag. */
	enum cmp_flag cmp_flag;

	/** The mode comparison flag. */
	enum mode_cmp mode_cmp;
	/** Mode to use for files. */
	mode_t file_mode;
	/** Mode to use for directories (different due to X). */
	mode_t dir_mode;

	/** The optional stat field to look at. */
	enum bfs_stat_field stat_field;
	/** The optional reference time. */
	struct timespec reftime;
	/** The optional time unit. */
	enum time_unit time_unit;

	/** The optional size unit. */
	enum size_unit size_unit;

	/** Optional device number for a target file. */
	dev_t dev;
	/** Optional inode number for a target file. */
	ino_t ino;

	/** File to output to. */
	CFILE *cfile;

	/** Optional compiled regex. */
	regex_t *regex;

	/** Optional exec command. */
	struct bfs_exec *execbuf;

	/** Optional printf command. */
	struct bfs_printf *printf;

	/** Optional integer data for this expression. */
	long long idata;

	/** Optional string data for this expression. */
	const char *sdata;

	/** The number of files this expression keeps open between evaluations. */
	int persistent_fds;
	/** The number of files this expression opens during evaluation. */
	int ephemeral_fds;
};

/** Singleton true expression instance. */
extern struct expr expr_true;
/** Singleton false expression instance. */
extern struct expr expr_false;

/**
 * Create a new expression.
 */
struct expr *new_expr(eval_fn *eval, size_t argc, char **argv);

/**
 * @return Whether expr is known to always quit.
 */
bool expr_never_returns(const struct expr *expr);

/**
 * @return The result of the comparison for this expression.
 */
bool expr_cmp(const struct expr *expr, long long n);

/**
 * Dump a parsed expression.
 */
void dump_expr(CFILE *cfile, const struct expr *expr, bool verbose);

/**
 * Free an expression tree.
 */
void free_expr(struct expr *expr);

#endif // BFS_EXPR_H
