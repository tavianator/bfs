/*********************************************************************
 * bfs                                                               *
 * Copyright (C) 2016 Tavian Barnes <tavianator@tavianator.com>      *
 *                                                                   *
 * This program is free software. It comes without any warranty, to  *
 * the extent permitted by applicable law. You can redistribute it   *
 * and/or modify it under the terms of the Do What The Fuck You Want *
 * To Public License, Version 2, as published by Sam Hocevar. See    *
 * the COPYING file or http://www.wtfpl.net/ for more details.       *
 *********************************************************************/

#ifndef BFS_UTIL_H
#define BFS_UTIL_H

#include <dirent.h>
#include <stdbool.h>
#include <sys/stat.h>

// Some portability concerns

#if __APPLE__
#	define st_atim st_atimespec
#	define st_ctim st_ctimespec
#	define st_mtim st_mtimespec
#endif

#ifndef S_ISDOOR
#	define S_ISDOOR(mode) false
#endif

#ifndef S_ISPORT
#	define S_ISPORT(mode) false
#endif

#ifndef S_ISWHT
#	define S_ISWHT(mode) false
#endif

/**
 * readdir() wrapper that makes error handling cleaner.
 */
int xreaddir(DIR *dir, struct dirent **de);

/**
 * Check if a file descriptor is open.
 */
bool isopen(int fd);

/**
 * Open a file and redirect it to a particular descriptor.
 *
 * @param fd
 *         The file descriptor to redirect.
 * @param path
 *         The path to open.
 * @param flags
 *         The flags passed to open().
 * @param mode
 *         The mode passed to open() (optional).
 * @return fd on success, -1 on failure.
 */
int redirect(int fd, const char *path, int flags, ...);

/**
 * Like dup(), but set the FD_CLOEXEC flag.
 *
 * @param fd
 *         The file descriptor to duplicate.
 * @return A duplicated file descriptor, or -1 on failure.
 */
int dup_cloexec(int fd);

#endif // BFS_UTIL_H
