// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

/**
 * A file-walking API based on nftw().
 */

#ifndef BFS_BFTW_H
#define BFS_BFTW_H

#include "dir.h"
#include "stat.h"
#include <stddef.h>

/**
 * Possible visit occurrences.
 */
enum bftw_visit {
	/** Pre-order visit. */
	BFTW_PRE,
	/** Post-order visit. */
	BFTW_POST,
};

/**
 * Cached bfs_stat() info for a file.
 */
struct bftw_stat {
	/** The bfs_stat(BFS_STAT_FOLLOW) buffer. */
	const struct bfs_stat *stat_buf;
	/** The bfs_stat(BFS_STAT_NOFOLLOW) buffer. */
	const struct bfs_stat *lstat_buf;
	/** The cached bfs_stat(BFS_STAT_FOLLOW) error. */
	int stat_err;
	/** The cached bfs_stat(BFS_STAT_NOFOLLOW) error. */
	int lstat_err;
};

/**
 * Data about the current file for the bftw() callback.
 */
struct BFTW {
	/** The path to the file. */
	const char *path;
	/** The string offset of the filename. */
	size_t nameoff;

	/** The root path passed to bftw(). */
	const char *root;
	/** The depth of this file in the traversal. */
	size_t depth;
	/** Which visit this is. */
	enum bftw_visit visit;

	/** The file type. */
	enum bfs_type type;
	/** The errno that occurred, if type == BFS_ERROR. */
	int error;

	/** A parent file descriptor for the *at() family of calls. */
	int at_fd;
	/** The path relative to at_fd for the *at() family of calls. */
	const char *at_path;

	/** Flags for bfs_stat(). */
	enum bfs_stat_flags stat_flags;
	/** Cached bfs_stat() info. */
	struct bftw_stat stat_bufs;
};

/**
 * Get bfs_stat() info for a file encountered during bftw(), caching the result
 * whenever possible.
 *
 * @param ftwbuf
 *         bftw() data for the file to stat.
 * @param flags
 *         flags for bfs_stat().  Pass ftwbuf->stat_flags for the default flags.
 * @return
 *         A pointer to a bfs_stat() buffer, or NULL if the call failed.
 */
const struct bfs_stat *bftw_stat(const struct BFTW *ftwbuf, enum bfs_stat_flags flags);

/**
 * Get bfs_stat() info for a file encountered during bftw(), if it has already
 * been cached.
 *
 * @param ftwbuf
 *         bftw() data for the file to stat.
 * @param flags
 *         flags for bfs_stat().  Pass ftwbuf->stat_flags for the default flags.
 * @return
 *         A pointer to a bfs_stat() buffer, or NULL if no stat info is cached.
 */
const struct bfs_stat *bftw_cached_stat(const struct BFTW *ftwbuf, enum bfs_stat_flags flags);

/**
 * Get the type of a file encountered during bftw(), with flags controlling
 * whether to follow links.  This function will avoid calling bfs_stat() if
 * possible.
 *
 * @param ftwbuf
 *         bftw() data for the file to check.
 * @param flags
 *         flags for bfs_stat().  Pass ftwbuf->stat_flags for the default flags.
 * @return
 *         The type of the file, or BFS_ERROR if an error occurred.
 */
enum bfs_type bftw_type(const struct BFTW *ftwbuf, enum bfs_stat_flags flags);

/**
 * Walk actions returned by the bftw() callback.
 */
enum bftw_action {
	/** Keep walking. */
	BFTW_CONTINUE,
	/** Skip this path's children. */
	BFTW_PRUNE,
	/** Stop walking. */
	BFTW_STOP,
};

/**
 * Callback function type for bftw().
 *
 * @param ftwbuf
 *         Data about the current file.
 * @param ptr
 *         The pointer passed to bftw().
 * @return
 *         An action value.
 */
typedef enum bftw_action bftw_callback(const struct BFTW *ftwbuf, void *ptr);

/**
 * Flags that control bftw() behavior.
 */
enum bftw_flags {
	/** stat() each encountered file. */
	BFTW_STAT          = 1 << 0,
	/** Attempt to recover from encountered errors. */
	BFTW_RECOVER       = 1 << 1,
	/** Visit directories in post-order as well as pre-order. */
	BFTW_POST_ORDER    = 1 << 2,
	/** If the initial path is a symbolic link, follow it. */
	BFTW_FOLLOW_ROOTS  = 1 << 3,
	/** Follow all symbolic links. */
	BFTW_FOLLOW_ALL    = 1 << 4,
	/** Detect directory cycles. */
	BFTW_DETECT_CYCLES = 1 << 5,
	/** Skip mount points and their descendents. */
	BFTW_SKIP_MOUNTS   = 1 << 6,
	/** Skip the descendents of mount points. */
	BFTW_PRUNE_MOUNTS  = 1 << 7,
	/** Sort directory entries before processing them. */
	BFTW_SORT          = 1 << 8,
	/** Read each directory into memory before processing its children. */
	BFTW_BUFFER        = 1 << 9,
	/** Include whiteouts in the search results. */
	BFTW_WHITEOUTS     = 1 << 10,
};

/**
 * Tree search strategies for bftw().
 */
enum bftw_strategy {
	/** Breadth-first search. */
	BFTW_BFS,
	/** Depth-first search. */
	BFTW_DFS,
	/** Iterative deepening search. */
	BFTW_IDS,
	/** Exponential deepening search. */
	BFTW_EDS,
};

/**
 * Structure for holding the arguments passed to bftw().
 */
struct bftw_args {
	/** The path(s) to start from. */
	const char **paths;
	/** The number of starting paths. */
	size_t npaths;

	/** The callback to invoke. */
	bftw_callback *callback;
	/** A pointer which is passed to the callback. */
	void *ptr;

	/** The maximum number of file descriptors to keep open. */
	int nopenfd;
	/** The maximum number of threads to use. */
	int nthreads;

	/** Flags that control bftw() behaviour. */
	enum bftw_flags flags;
	/** The search strategy to use. */
	enum bftw_strategy strategy;

	/** The parsed mount table, if available. */
	const struct bfs_mtab *mtab;
};

/**
 * Breadth First Tree Walk (or Better File Tree Walk).
 *
 * Like ftw(3) and nftw(3), this function walks a directory tree recursively,
 * and invokes a callback for each path it encounters.
 *
 * @param args
 *         The arguments that control the walk.
 * @return
 *         0 on success, or -1 on failure.
 */
int bftw(const struct bftw_args *args);

#endif // BFS_BFTW_H
