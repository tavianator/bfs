// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

/**
 * The expression tree representation.
 */

#ifndef BFS_EXPR_H
#define BFS_EXPR_H

#include "color.h"
#include "config.h"
#include "eval.h"
#include "stat.h"
#include <stddef.h>
#include <sys/types.h>
#include <time.h>

/**
 * Integer comparison modes.
 */
enum bfs_int_cmp {
	/** Exactly N. */
	BFS_INT_EQUAL,
	/** Less than N (-N). */
	BFS_INT_LESS,
	/** Greater than N (+N). */
	BFS_INT_GREATER,
};

/**
 * Permission comparison modes.
 */
enum bfs_mode_cmp {
	/** Mode is an exact match (MODE). */
	BFS_MODE_EQUAL,
	/** Mode has all these bits (-MODE). */
	BFS_MODE_ALL,
	/** Mode has any of these bits (/MODE). */
	BFS_MODE_ANY,
};

/**
 * Possible time units.
 */
enum bfs_time_unit {
	/** Seconds. */
	BFS_SECONDS,
	/** Minutes. */
	BFS_MINUTES,
	/** Days. */
	BFS_DAYS,
};

/**
 * Possible file size units.
 */
enum bfs_size_unit {
	/** 512-byte blocks. */
	BFS_BLOCKS,
	/** Single bytes. */
	BFS_BYTES,
	/** Two-byte words. */
	BFS_WORDS,
	/** Kibibytes. */
	BFS_KB,
	/** Mebibytes. */
	BFS_MB,
	/** Gibibytes. */
	BFS_GB,
	/** Tebibytes. */
	BFS_TB,
	/** Pebibytes. */
	BFS_PB,
};

/**
 * A command line expression.
 */
struct bfs_expr {
	/** The function that evaluates this expression. */
	bfs_eval_fn *eval_fn;

	/** The number of command line arguments for this expression. */
	size_t argc;
	/** The command line arguments comprising this expression. */
	char **argv;

	/** The number of files this expression keeps open between evaluations. */
	int persistent_fds;
	/** The number of files this expression opens during evaluation. */
	int ephemeral_fds;

	/** Whether this expression has no side effects. */
	bool pure;
	/** Whether this expression always evaluates to true. */
	bool always_true;
	/** Whether this expression always evaluates to false. */
	bool always_false;

	/** Estimated cost. */
	float cost;
	/** Estimated probability of success. */
	float probability;
	/** Number of times this predicate was evaluated. */
	size_t evaluations;
	/** Number of times this predicate succeeded. */
	size_t successes;
	/** Total time spent running this predicate. */
	struct timespec elapsed;

	/** Auxilliary data for the evaluation function. */
	union {
		/** Child expressions. */
		struct {
			/** The left hand side of the expression. */
			struct bfs_expr *lhs;
			/** The right hand side of the expression. */
			struct bfs_expr *rhs;
		};

		/** Integer comparisons. */
		struct {
			/** Integer for this comparison. */
			long long num;
			/** The comparison mode. */
			enum bfs_int_cmp int_cmp;

			/** Optional extra data. */
			union {
				/** -size data. */
				enum bfs_size_unit size_unit;

				/** Timestamp comparison data. */
				struct {
					/** The stat field to look at. */
					enum bfs_stat_field stat_field;
					/** The reference time. */
					struct timespec reftime;
					/** The time unit. */
					enum bfs_time_unit time_unit;
				};
			};
		};

		/** String comparisons. */
		struct {
			/** String pattern. */
			const char *pattern;
			/** fnmatch() flags. */
			int fnm_flags;
			/** Whether strcmp() can be used instead of fnmatch(). */
			bool literal;
		};

		/** Printing actions. */
		struct {
			/** The output stream. */
			CFILE *cfile;
			/** Optional file path. */
			const char *path;
			/** Optional -printf format. */
			struct bfs_printf *printf;
		};

		/** -exec data. */
		struct bfs_exec *exec;

		/** -flags data. */
		struct {
			/** The comparison mode. */
			enum bfs_mode_cmp flags_cmp;
			/** Flags that should be set. */
			unsigned long long set_flags;
			/** Flags that should be cleared. */
			unsigned long long clear_flags;
		};

		/** -perm data. */
		struct {
			/** The comparison mode. */
			enum bfs_mode_cmp mode_cmp;
			/** Mode to use for files. */
			mode_t file_mode;
			/** Mode to use for directories (different due to X). */
			mode_t dir_mode;
		};

		/** -regex data. */
		struct bfs_regex *regex;

		/** -samefile data. */
		struct {
			/** Device number of the target file. */
			dev_t dev;
			/** Inode number of the target file. */
			ino_t ino;
		};
	};
};

/**
 * Create a new expression.
 */
struct bfs_expr *bfs_expr_new(bfs_eval_fn *eval, size_t argc, char **argv);

/**
 * @return Whether the expression has child expressions.
 */
bool bfs_expr_is_parent(const struct bfs_expr *expr);

/**
 * @return Whether expr is known to always quit.
 */
bool bfs_expr_never_returns(const struct bfs_expr *expr);

/**
 * @return The result of the integer comparison for this expression.
 */
bool bfs_expr_cmp(const struct bfs_expr *expr, long long n);

/**
 * Free an expression tree.
 */
void bfs_expr_free(struct bfs_expr *expr);

#endif // BFS_EXPR_H
