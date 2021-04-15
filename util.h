/****************************************************************************
 * bfs                                                                      *
 * Copyright (C) 2016-2021 Tavian Barnes <tavianator@tavianator.com>        *
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
#include <regex.h>
#include <stdbool.h>
#include <stddef.h>
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

#ifndef BFS_HAS_UTIL
#	define BFS_HAS_UTIL BFS_HAS_INCLUDE(<util.h>, __NetBSD__)
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

// Lower bound on BFS_FLEX_SIZEOF()
#define BFS_FLEX_LB(type, member, length) (offsetof(type, member) + sizeof(((type *)NULL)->member[0]) * (length))

// Maximum macro for BFS_FLEX_SIZE()
#define BFS_FLEX_MAX(a, b) ((a) > (b) ? (a) : (b))

/**
 * Computes the size of a struct containing a flexible array member of the given
 * length.
 *
 * @param type
 *         The type of the struct containing the flexible array.
 * @param member
 *         The name of the flexible array member.
 * @param length
 *         The length of the flexible array.
 */
#define BFS_FLEX_SIZEOF(type, member, length) \
	(sizeof(type) <= BFS_FLEX_LB(type, member, 0) \
		? BFS_FLEX_LB(type, member, length) \
		: BFS_FLEX_MAX(sizeof(type), BFS_FLEX_LB(type, member, length)))

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

#endif // BFS_UTIL_H
