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

#ifndef BFS_BFTW_H
#define BFS_BFTW_H

#include "stat.h"
#include <stddef.h>

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
 * Possible visit occurrences.
 */
enum bftw_visit {
	/** Pre-order visit. */
	BFTW_PRE,
	/** Post-order visit. */
	BFTW_POST,
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

	/** A bfs_stat() buffer; may be NULL if no stat() call was needed. */
	const struct bfs_stat *statbuf;

	/** A parent file descriptor for the *at() family of calls. */
	int at_fd;
	/** The path relative to atfd for the *at() family of calls. */
	const char *at_path;
	/** Appropriate flags (such as AT_SYMLINK_NOFOLLOW) for the *at() family of calls. */
	int at_flags;
};

/**
 * Walk actions returned by the bftw() callback.
 */
enum bftw_action {
	/** Keep walking. */
	BFTW_CONTINUE,
	/** Skip this path's siblings. */
	BFTW_SKIP_SIBLINGS,
	/** Skip this path's children. */
	BFTW_SKIP_SUBTREE,
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
typedef enum bftw_action bftw_fn(struct BFTW *ftwbuf, void *ptr);

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
	/** Stay on the same filesystem. */
	BFTW_XDEV          = 1 << 6,
};

/**
 * Breadth First Tree Walk (or Better File Tree Walk).
 *
 * Like ftw(3) and nftw(3), this function walks a directory tree recursively,
 * and invokes a callback for each path it encounters.  However, bftw() operates
 * breadth-first.
 *
 * @param path
 *         The starting path.
 * @param fn
 *         The callback to invoke.
 * @param nopenfd
 *         The maximum number of file descriptors to keep open.
 * @param flags
 *         Flags that control bftw() behavior.
 * @param ptr
 *         A generic pointer which is passed to fn().
 * @return
 *         0 on success, or -1 on failure.
 */
int bftw(const char *path, bftw_fn *fn, int nopenfd, enum bftw_flags flags, void *ptr);

#endif // BFS_BFTW_H
