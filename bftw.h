/*********************************************************************
 * bfs                                                               *
 * Copyright (C) 2015-2016 Tavian Barnes <tavianator@tavianator.com> *
 *                                                                   *
 * This program is free software. It comes without any warranty, to  *
 * the extent permitted by applicable law. You can redistribute it   *
 * and/or modify it under the terms of the Do What The Fuck You Want *
 * To Public License, Version 2, as published by Sam Hocevar. See    *
 * the COPYING file or http://www.wtfpl.net/ for more details.       *
 *********************************************************************/

#ifndef BFS_BFTW_H
#define BFS_BFTW_H

#include <stddef.h>
#include <sys/stat.h>

/**
 * Possible file types.
 */
enum bftw_typeflag {
	/** Unknown type. */
	BFTW_UNKNOWN,
	/** Block device. */
	BFTW_BLK,
	/** Character device. */
	BFTW_CHR,
	/** Directory. */
	BFTW_DIR,
	/** Solaris door. */
	BFTW_DOOR,
	/** Pipe. */
	BFTW_FIFO,
	/** Symbolic link. */
	BFTW_LNK,
	/** Solaris event port. */
	BFTW_PORT,
	/** Regular file. */
	BFTW_REG,
	/** Socket. */
	BFTW_SOCK,
	/** BSD whiteout. */
	BFTW_WHT,
	/** An error occurred for this file. */
	BFTW_ERROR,
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

	/** The depth of this file in the traversal. */
	size_t depth;
	/** Which visit this is. */
	enum bftw_visit visit;

	/** The file type. */
	enum bftw_typeflag typeflag;
	/** The errno that occurred, if typeflag == BFTW_ERROR. */
	int error;

	/** A stat() buffer; may be NULL if no stat() call was needed. */
	const struct stat *statbuf;

	/** A parent file descriptor for the *at() family of calls. */
	int at_fd;
	/** The path relative to atfd for the *at() family of calls. */
	const char *at_path;
	/** Appropriate flags (such as AT_SYMLINK_NOFOLLOW) for the *at() family of calls. */
	int at_flags;
};

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
