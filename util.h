/****************************************************************************
 * bfs                                                                      *
 * Copyright (C) 2016-2017 Tavian Barnes <tavianator@tavianator.com>        *
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

#ifndef BFS_UTIL_H
#define BFS_UTIL_H

#include "bftw.h"
#include <dirent.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <regex.h>
#include <stdbool.h>
#include <sys/types.h>
#include <time.h>

// Some portability concerns

#if !defined(FNM_CASEFOLD) && defined(FNM_IGNORECASE)
#	define FNM_CASEFOLD FNM_IGNORECASE
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

#ifndef O_DIRECTORY
#	define O_DIRECTORY 0
#endif

/**
 * readdir() wrapper that makes error handling cleaner.
 */
int xreaddir(DIR *dir, struct dirent **de);

/**
 * readlinkat() wrapper that dynamically allocates the result.
 *
 * @param fd
 *         The base directory descriptor.
 * @param path
 *         The path to the link, relative to fd.
 * @param size
 *         An estimate for the size of the link name (pass 0 if unknown).
 * @return The target of the link, allocated with malloc(), or NULL on failure.
 */
char *xreadlinkat(int fd, const char *path, size_t size);

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

/**
 * Like pipe(), but set the FD_CLOEXEC flag.
 *
 * @param pipefd
 *         The array to hold the two file descriptors.
 * @return 0 on success, -1 on failure.
 */
int pipe_cloexec(int pipefd[2]);

/**
 * Dynamically allocate a regex error message.
 *
 * @param err
 *         The error code to stringify.
 * @param regex
 *         The (partially) compiled regex.
 * @return A human-readable description of the error, allocated with malloc().
 */
char *xregerror(int err, const regex_t *regex);

/**
 * localtime_r() wrapper that calls tzset() first.
 *
 * @param timep
 *         The time_t to convert.
 * @param result
 *         Buffer to hold the result.
 * @return 0 on success, -1 on failure.
 */
int xlocaltime(const time_t *timep, struct tm *result);

/**
 * Format a mode like ls -l (e.g. -rw-r--r--).
 *
 * @param mode
 *         The mode to format.
 * @param str
 *         The string to hold the formatted mode.
 */
void format_mode(mode_t mode, char str[11]);

/**
 * basename() variant that doesn't modify the input.
 *
 * @param path
 *         The path in question.
 * @return A pointer into path at the base name offset.
 */
const char *xbasename(const char *path);

/**
 * Wrapper for faccessat() that handles some portability issues.
 */
int xfaccessat(int fd, const char *path, int amode);

/**
 * Return whether an error code is due to a path not existing.
 */
bool is_nonexistence_error(int error);

/**
 * Convert a bfs_stat() mode to a bftw() typeflag.
 */
enum bftw_typeflag mode_to_typeflag(mode_t mode);

/**
 * Convert a directory entry to a bftw() typeflag.
 */
enum bftw_typeflag dirent_to_typeflag(const struct dirent *de);

/**
 * Process a yes/no prompt.
 *
 * @return 1 for yes, 0 for no, and -1 for unknown.
 */
int ynprompt(void);

#endif // BFS_UTIL_H
