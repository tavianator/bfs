/****************************************************************************
 * bfs                                                                      *
 * Copyright (C) 2015-2022 Tavian Barnes <tavianator@tavianator.com>        *
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
 * bfs execution context.
 */

#ifndef BFS_CTX_H
#define BFS_CTX_H

#include "bftw.h"
#include "trie.h"
#include <stdbool.h>
#include <sys/resource.h>

/**
 * Various debugging flags.
 */
enum debug_flags {
	/** Print cost estimates. */
	DEBUG_COST   = 1 << 0,
	/** Print executed command details. */
	DEBUG_EXEC   = 1 << 1,
	/** Print optimization details. */
	DEBUG_OPT    = 1 << 2,
	/** Print rate information. */
	DEBUG_RATES  = 1 << 3,
	/** Trace the filesystem traversal. */
	DEBUG_SEARCH = 1 << 4,
	/** Trace all stat() calls. */
	DEBUG_STAT   = 1 << 5,
	/** Print the parse tree. */
	DEBUG_TREE   = 1 << 6,
	/** All debug flags. */
	DEBUG_ALL    = (1 << 7) - 1,
};

/**
 * Convert a debug flag to a string.
 */
const char *debug_flag_name(enum debug_flags flag);

/**
 * The execution context for bfs.
 */
struct bfs_ctx {
	/** The unparsed command line arguments. */
	char **argv;
	/** The root paths. */
	const char **paths;
	/** The main command line expression. */
	struct expr *expr;
	/** An expression for files to filter out. */
	struct expr *exclude;

	/** -mindepth option. */
	int mindepth;
	/** -maxdepth option. */
	int maxdepth;

	/** bftw() flags. */
	enum bftw_flags flags;
	/** bftw() search strategy. */
	enum bftw_strategy strategy;

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

	/** User table. */
	struct bfs_users *users;
	/** The error that occurred parsing the user table, if any. */
	int users_error;
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

	/** The initial RLIMIT_NOFILE soft limit. */
	rlim_t nofile_soft;
	/** The initial RLIMIT_NOFILE hard limit. */
	rlim_t nofile_hard;
};

/**
 * @return
 *         A new bfs context, or NULL on failure.
 */
struct bfs_ctx *bfs_ctx_new(void);

/**
 * Get the users table.
 *
 * @param ctx
 *         The bfs context.
 * @return
 *         The cached users table, or NULL on failure.
 */
const struct bfs_users *bfs_ctx_users(const struct bfs_ctx *ctx);

/**
 * Get the groups table.
 *
 * @param ctx
 *         The bfs context.
 * @return
 *         The cached groups table, or NULL on failure.
 */
const struct bfs_groups *bfs_ctx_groups(const struct bfs_ctx *ctx);

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
