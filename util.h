/****************************************************************************
 * bfs                                                                      *
 * Copyright (C) 2016-2020 Tavian Barnes <tavianator@tavianator.com>        *
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

#include <dirent.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <regex.h>
#include <stdbool.h>
#include <sys/types.h>

// Some portability concerns

#ifdef __has_feature
#	define BFS_HAS_FEATURE(feature, fallback) __has_feature(feature)
#else
#	define BFS_HAS_FEATURE(feature, fallback) fallback
#endif

#ifdef __has_include
#	define BFS_HAS_INCLUDE(header, fallback) __has_include(header)
#else
#	define BFS_HAS_INCLUDE(header, fallback) fallback
#endif

#ifndef BFS_HAS_MNTENT
#	define BFS_HAS_MNTENT BFS_HAS_INCLUDE(<mntent.h>, __GLIBC__)
#endif

#ifndef BFS_HAS_SYS_ACL
#	define BFS_HAS_SYS_ACL BFS_HAS_INCLUDE(<sys/acl.h>, true)
#endif

#ifndef BFS_HAS_SYS_CAPABILITY
#	define BFS_HAS_SYS_CAPABILITY BFS_HAS_INCLUDE(<sys/capability.h>, __linux__)
#endif

#ifndef BFS_HAS_SYS_EXTATTR
#	define BFS_HAS_SYS_EXTATTR BFS_HAS_INCLUDE(<sys/extattr.h>, __FreeBSD__)
#endif

#ifndef BFS_HAS_SYS_MKDEV
#	define BFS_HAS_SYS_MKDEV BFS_HAS_INCLUDE(<sys/mkdev.h>, false)
#endif

#ifndef BFS_HAS_SYS_PARAM
#	define BFS_HAS_SYS_PARAM BFS_HAS_INCLUDE(<sys/param.h>, true)
#endif

#ifndef BFS_HAS_SYS_SYSMACROS
#	define BFS_HAS_SYS_SYSMACROS BFS_HAS_INCLUDE(<sys/sysmacros.h>, __GLIBC__)
#endif

#ifndef BFS_HAS_SYS_XATTR
#	define BFS_HAS_SYS_XATTR BFS_HAS_INCLUDE(<sys/xattr.h>, __linux__)
#endif

#if !defined(FNM_CASEFOLD) && defined(FNM_IGNORECASE)
#	define FNM_CASEFOLD FNM_IGNORECASE
#endif

#ifndef O_DIRECTORY
#	define O_DIRECTORY 0
#endif

/**
 * Adds compiler warnings for bad printf()-style function calls, if supported.
 */
#if __GNUC__
#	define BFS_FORMATTER(fmt, args) __attribute__((format(printf, fmt, args)))
#else
#	define BFS_FORMATTER(fmt, args)
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

#endif // BFS_UTIL_H
