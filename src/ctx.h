// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

/**
 * bfs execution context.
 */

#ifndef BFS_CTX_H
#define BFS_CTX_H

#include "bftw.h"
#include "config.h"
#include "diag.h"
#include "trie.h"
#include <stddef.h>
#include <sys/resource.h>
#include <time.h>

/**
 * The execution context for bfs.
 */
struct bfs_ctx {
	/** The number of command line arguments. */
	size_t argc;
	/** The unparsed command line arguments. */
	char **argv;

	/** The root paths. */
	const char **paths;
	/** The number of root paths. */
	size_t npaths;

	/** The main command line expression. */
	struct bfs_expr *expr;
	/** An expression for files to filter out. */
	struct bfs_expr *exclude;

	/** -mindepth option. */
	int mindepth;
	/** -maxdepth option. */
	int maxdepth;

	/** bftw() flags. */
	enum bftw_flags flags;
	/** bftw() search strategy. */
	enum bftw_strategy strategy;

	/** Threads (-j). */
	int threads;
	/** Optimization level (-O). */
	int optlevel;
	/** Debugging flags (-D). */
	enum debug_flags debug;
	/** Whether to ignore deletions that race with bfs (-ignore_readdir_race). */
	bool ignore_races;
	/** Whether to follow POSIXisms more closely ($POSIXLY_CORRECT). */
	bool posixly_correct;
	/** Whether to show a status bar (-status). */
	bool status;
	/** Whether to only return unique files (-unique). */
	bool unique;
	/** Whether to print warnings (-warn/-nowarn). */
	bool warn;
	/** Whether to only handle paths with xargs-safe characters (-X). */
	bool xargs_safe;

	/** Color data. */
	struct colors *colors;
	/** The error that occurred parsing the color table, if any. */
	int colors_error;
	/** Colored stdout. */
	struct CFILE *cout;
	/** Colored stderr. */
	struct CFILE *cerr;

	/** User cache. */
	struct bfs_users *users;
	/** Group table. */
	struct bfs_groups *groups;
	/** The error that occurred parsing the group table, if any. */
	int groups_error;

	/** Table of mounted file systems. */
	struct bfs_mtab *mtab;
	/** The error that occurred parsing the mount table, if any. */
	int mtab_error;

	/** All the files owned by the context. */
	struct trie files;
	/** The number of files owned by the context. */
	int nfiles;

	/** The initial RLIMIT_NOFILE limits. */
	struct rlimit orig_nofile;
	/** The current RLIMIT_NOFILE limits. */
	struct rlimit cur_nofile;

	/** The current time. */
	struct timespec now;
};

/**
 * @return
 *         A new bfs context, or NULL on failure.
 */
struct bfs_ctx *bfs_ctx_new(void);

/**
 * Get the mount table.
 *
 * @param ctx
 *         The bfs context.
 * @return
 *         The cached mount table, or NULL on failure.
 */
const struct bfs_mtab *bfs_ctx_mtab(const struct bfs_ctx *ctx);

/**
 * Deduplicate an opened file.
 *
 * @param ctx
 *         The bfs context.
 * @param cfile
 *         The opened file.
 * @param path
 *         The path to the opened file (or NULL for standard streams).
 * @return
 *         If the same file was opened previously, that file is returned.  If cfile is a new file,
 *         cfile itself is returned.  If an error occurs, NULL is returned.
 */
struct CFILE *bfs_ctx_dedup(struct bfs_ctx *ctx, struct CFILE *cfile, const char *path);

/**
 * Flush any caches for consistency with external processes.
 *
 * @param ctx
 *         The bfs context.
 */
void bfs_ctx_flush(const struct bfs_ctx *ctx);

/**
 * Dump the parsed command line.
 *
 * @param ctx
 *         The bfs context.
 * @param flag
 *         The -D flag that triggered the dump.
 */
void bfs_ctx_dump(const struct bfs_ctx *ctx, enum debug_flags flag);

/**
 * Free a bfs context.
 *
 * @param ctx
 *         The context to free.
 * @return
 *         0 on success, -1 if any errors occurred.
 */
int bfs_ctx_free(struct bfs_ctx *ctx);

#endif // BFS_CTX_H
