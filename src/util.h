/****************************************************************************
 * bfs                                                                      *
 * Copyright (C) 2016-2022 Tavian Barnes <tavianator@tavianator.com>        *
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
 * Assorted utilities that don't belong anywhere else.
 */

#ifndef BFS_UTIL_H
#define BFS_UTIL_H

#include <fcntl.h>
#include <fnmatch.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/types.h>

// Some portability concerns

#if !defined(FNM_CASEFOLD) && defined(FNM_IGNORECASE)
#	define FNM_CASEFOLD FNM_IGNORECASE
#endif

#ifndef O_DIRECTORY
#	define O_DIRECTORY 0
#endif

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
 * Format a mode like ls -l (e.g. -rw-r--r--).
 *
 * @param mode
 *         The mode to format.
 * @param str
 *         The string to hold the formatted mode.
 */
void xstrmode(mode_t mode, char str[11]);

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
 * Portability wrapper for strtofflags().
 *
 * @param str
 *         The string to parse.  The pointee will be advanced to the first
 *         invalid position on error.
 * @param set
 *         The flags that are set in the string.
 * @param clear
 *         The flags that are cleared in the string.
 * @return
 *         0 on success, -1 on failure.
 */
int xstrtofflags(const char **str, unsigned long long *set, unsigned long long *clear);

/**
 * wcswidth() variant that works on narrow strings.
 *
 * @param str
 *         The string to measure.
 * @return
 *         The likely width of that string in a terminal.
 */
size_t xstrwidth(const char *str);

/**
 * Return whether an error code is due to a path not existing.
 */
bool is_nonexistence_error(int error);

/**
 * Process a yes/no prompt.
 *
 * @return 1 for yes, 0 for no, and -1 for unknown.
 */
int ynprompt(void);

/**
 * Portable version of makedev().
 */
dev_t bfs_makedev(int ma, int mi);

/**
 * Portable version of major().
 */
int bfs_major(dev_t dev);

/**
 * Portable version of minor().
 */
int bfs_minor(dev_t dev);

/**
 * A safe version of read() that handles interrupted system calls and partial
 * reads.
 *
 * @return
 *         The number of bytes read.  A value != nbytes indicates an error
 *         (errno != 0) or end of file (errno == 0).
 */
size_t xread(int fd, void *buf, size_t nbytes);

/**
 * A safe version of write() that handles interrupted system calls and partial
 * writes.
 *
 * @return
           The number of bytes written.  A value != nbytes indicates an error.
 */
size_t xwrite(int fd, const void *buf, size_t nbytes);

/**
 * Wrapper for confstr() that allocates with malloc().
 *
 * @param name
 *         The ID of the confstr to look up.
 * @return
 *         The value of the confstr, or NULL on failure.
 */
char *xconfstr(int name);

/**
 * Convenience wrapper for getdelim().
 *
 * @param file
 *         The file to read.
 * @param delim
 *         The delimiter character to split on.
 * @return
 *         The read chunk (without the delimiter), allocated with malloc().
 *         NULL is returned on error (errno != 0) or end of file (errno == 0).
 */
char *xgetdelim(FILE *file, char delim);

/**
 * fopen() variant that takes open() style flags.
 *
 * @param path
 *         The path to open.
 * @param flags
 *         Flags to pass to open().
 */
FILE *xfopen(const char *path, int flags);

/**
 * close() wrapper that asserts the file descriptor is valid.
 *
 * @param fd
 *         The file descriptor to close.
 * @return
 *         0 on success, or -1 on error.
 */
int xclose(int fd);

/**
 * close() variant that preserves errno.
 *
 * @param fd
 *         The file descriptor to close.
 */
void close_quietly(int fd);

#endif // BFS_UTIL_H
