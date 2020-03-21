/****************************************************************************
 * bfs                                                                      *
 * Copyright (C) 2015-2019 Tavian Barnes <tavianator@tavianator.com>        *
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
 * A file-walking API based on nftw().
 */

#ifndef BFS_BFTW_H
#define BFS_BFTW_H

#include "mtab.h"
#include "stat.h"
#include <stddef.h>
#include <sys/types.h>

/**
 * Possible file types.
 */
enum bftw_typeflag {
	/** Unknown type. */
	BFTW_UNKNOWN = 0,
	/** Block device. */
	BFTW_BLK     = 1 << 0,
	/** Character device. */
	BFTW_CHR     = 1 << 1,
	/** Directory. */
	BFTW_DIR     = 1 << 2,
	/** Solaris door. */
	BFTW_DOOR    = 1 << 3,
	/** Pipe. */
	BFTW_FIFO    = 1 << 4,
	/** Symbolic link. */
	BFTW_LNK     = 1 << 5,
	/** Solaris event port. */
	BFTW_PORT    = 1 << 6,
	/** Regular file. */
	BFTW_REG     = 1 << 7,
	/** Socket. */
	BFTW_SOCK    = 1 << 8,
	/** BSD whiteout. */
	BFTW_WHT     = 1 << 9,
	/** An error occurred for this file. */
	BFTW_ERROR   = 1 << 10,
};

/**
 * Convert a bfs_stat() mode to a bftw() typeflag.
 */
enum bftw_typeflag bftw_mode_typeflag(mode_t mode);

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
	/** A pointer to the bfs_stat() buffer, if available. */
	const struct bfs_stat *buf;
	/** Storage for the bfs_stat() buffer, if needed. */
	struct bfs_stat storage;
	/** The cached error code, if any. */
	int error;
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
	enum bftw_typeflag typeflag;
	/** The errno that occurred, if typeflag == BFTW_ERROR. */
	int error;

	/** A parent file descriptor for the *at() family of calls. */
	int at_fd;
	/** The path relative to at_fd for the *at() family of calls. */
	const char *at_path;

	/** Flags for bfs_stat(). */
	enum bfs_stat_flag stat_flags;
	/** Cached bfs_stat() info for BFS_STAT_NOFOLLOW. */
	struct bftw_stat lstat_cache;
	/** Cached bfs_stat() info for BFS_STAT_FOLLOW. */
	struct bftw_stat stat_cache;
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
const struct bfs_stat *bftw_stat(const struct BFTW *ftwbuf, enum bfs_stat_flag flags);

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
 *         The type of the file, or BFTW_ERROR if an error occurred.
 */
enum bftw_typeflag bftw_typeflag(const struct BFTW *ftwbuf, enum bfs_stat_flag flags);

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
	BFTW_DEPTH         = 1 << 2,
	/** If the initial path is a symbolic link, follow it. */
	BFTW_COMFOLLOW     = 1 << 3,
	/** Follow all symbolic links. */
	BFTW_LOGICAL       = 1 << 4,
	/** Detect directory cycles. */
	BFTW_DETECT_CYCLES = 1 << 5,
	/** Skip mount points and their descendents. */
	BFTW_MOUNT         = 1 << 6,
	/** Skip the descendents of mount points. */
	BFTW_XDEV          = 1 << 7,
	/** Sort directory entries before processing them. */
	BFTW_SORT          = 1 << 8,
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
