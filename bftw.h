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

#include <sys/stat.h>

/**
 * Callback function type for bftw().
 *
 * @param fpath
 *         The path to the encountered file.
 * @param sb
 *         A stat() buffer; may be NULL if no stat() call was needed.
 * @param typeflag
 *         A typeflag value (see below).
 * @param ptr
 *         The pointer passed to bftw().
 * @return
 *         An action value (see below).
 */
typedef int bftw_fn(const char *fpath, const struct stat *sb, int typeflag, void *ptr);

/**
 * Breadth First Tree Walk (or Better File Tree Walk).
 *
 * Like ftw(3) and nftw(3), this function walks a directory tree recursively,
 * and invokes a callback for each path it encounters.  However, bftw() operates
 * breadth-first.
 *
 * @param dirpath
 *         The starting path.
 * @param fn
 *         The callback to invoke.
 * @param nopenfd
 *         The maximum number of file descriptors to keep open.
 * @param flags
 *         Flags that control bftw() behavior (see below).
 * @param ptr
 *         A generic pointer which is passed to fn().
 * @return
 *         0 on success, or -1 on failure.
 */
int bftw(const char *dirpath, bftw_fn *fn, int nopenfd, int flags, void *ptr);

/** typeflag: Directory. */
#define BFTW_D        0
/** typeflag: Regular file. */
#define BFTW_R        1
/** typeflag: Symbolic link. */
#define BFTW_SL       2
/** typeflag: Unknown type. */
#define BFTW_UNKNOWN  3

/** action: Keep walking. */
#define BFTW_CONTINUE       0
/** action: Skip this path's siblings. */
#define BFTW_SKIP_SIBLINGS  1
/** action: Skip this path's children. */
#define BFTW_SKIP_SUBTREE   2
/** action: Stop walking. */
#define BFTW_STOP           3

/** flag: stat() each encountered file. */
#define BFTW_STAT  (1 << 0)
