// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

/**
 * The expression tree representation.
 */

#ifndef BFS_EXPR_H
#define BFS_EXPR_H

#include "color.h"
#include "eval.h"
#include "stat.h"

#include <sys/types.h>
#include <time.h>

/**
 * Argument/token/expression kinds.
 */
enum bfs_kind {
	/** A flag (-H, -L, etc.). */
	BFS_FLAG,

	/** A root path. */
	BFS_PATH,

	/** An option (-follow, -mindepth, etc.). */
	BFS_OPTION,
	/** A test (-name, -size, etc.). */
	BFS_TEST,
	/** An action (-print, -exec, etc.). */
	BFS_ACTION,

	/** An operator (-and, -or, etc.). */
	BFS_OPERATOR,
};

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
 * A linked list of expressions.
 */
struct bfs_exprs {
	struct bfs_expr *head;
	struct bfs_expr **tail;
};

/**
 * A command line expression.
 */
struct bfs_expr {
	/** This expression's next sibling, if any. */
	struct bfs_expr *next;
	/** The next allocated expression. */
	struct { struct bfs_expr *next; } freelist;

	/** The function that evaluates this expression. */
	bfs_eval_fn *eval_fn;

	/** The number of command line arguments for this expression. */
	size_t argc;
	/** The command line arguments comprising this expression. */
	char **argv;
	/** The kind of expression this is. */
	enum bfs_kind kind;

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
	/** Whether this expression uses stat(). */
	bool calls_stat;

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
		struct bfs_exprs children;

		/** Integer comparisons. */
		struct {
			/** Integer for this comparison. */
			long long num;
			/** The comparison mode. */
			enum bfs_int_cmp int_cmp;

			/** -size data. */
			enum bfs_size_unit size_unit;

			/** The stat field to look at. */
			enum bfs_stat_field stat_field;
			/** The time unit. */
			enum bfs_time_unit time_unit;
			/** The reference time. */
			struct timespec reftime;
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

struct bfs_ctx;

/**
 * Create a new expression.
 */
struct bfs_expr *bfs_expr_new(struct bfs_ctx *ctx, bfs_eval_fn *eval, size_t argc, char **argv, enum bfs_kind kind);

/**
 * @return Whether this type of expression has children.
 */
bool bfs_expr_is_parent(const struct bfs_expr *expr);

/**
 * @return The first child of this expression, or NULL if it has none.
 */
struct bfs_expr *bfs_expr_children(const struct bfs_expr *expr);

/**
 * Add a child to an expression.
 */
void bfs_expr_append(struct bfs_expr *expr, struct bfs_expr *child);

/**
 * Add a list of children to an expression.
 */
void bfs_expr_extend(struct bfs_expr *expr, struct bfs_exprs *children);

/**
 * @return Whether expr is known to always quit.
 */
bool bfs_expr_never_returns(const struct bfs_expr *expr);

/**
 * @return The result of the integer comparison for this expression.
 */
bool bfs_expr_cmp(const struct bfs_expr *expr, long long n);

/**
 * Free any resources owned by an expression.
 */
void bfs_expr_clear(struct bfs_expr *expr);

/**
 * Iterate over the children of an expression.
 */
#define for_expr(child, expr) \
	for (struct bfs_expr *child = bfs_expr_children(expr); child; child = child->next)

#endif // BFS_EXPR_H
