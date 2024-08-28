// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

/**
 * A dynamic string library.
 */

#ifndef BFS_DSTRING_H
#define BFS_DSTRING_H

#include "bfs.h"
#include "bfstd.h"

#include <stdarg.h>
#include <stddef.h>

/** Marker type for dynamic strings. */
#if BFS_LINT && __clang__
// Abuse __attribute__(aligned) to make a type that allows
//
//     dchar * -> char *
//
// conversions, but warns (with Clang's -Walign-mismatch) on
//
//     char * -> dchar *
typedef __attribute__((aligned(alignof(size_t)))) char dchar;
#else
typedef char dchar;
#endif

/**
 * Free a dynamic string.
 *
 * @param dstr
 *         The string to free.
 */
void dstrfree(dchar *dstr);

/**
 * Allocate a dynamic string.
 *
 * @param cap
 *         The initial capacity of the string.
 */
_malloc(dstrfree, 1)
dchar *dstralloc(size_t cap);

/**
 * Create a dynamic copy of a string.
 *
 * @param str
 *         The NUL-terminated string to copy.
 */
_malloc(dstrfree, 1)
dchar *dstrdup(const char *str);

/**
 * Create a length-limited dynamic copy of a string.
 *
 * @param str
 *         The string to copy.
 * @param n
 *         The maximum number of characters to copy from str.
 */
_malloc(dstrfree, 1)
dchar *dstrndup(const char *str, size_t n);

/**
 * Create a dynamic copy of a dynamic string.
 *
 * @param dstr
 *         The dynamic string to copy.
 */
_malloc(dstrfree, 1)
dchar *dstrddup(const dchar *dstr);

/**
 * Create an exact-sized dynamic copy of a string.
 *
 * @param str
 *         The string to copy.
 * @param len
 *         The length of the string, which may include internal NUL bytes.
 */
_malloc(dstrfree, 1)
dchar *dstrxdup(const char *str, size_t len);

/**
 * Get a dynamic string's length.
 *
 * @param dstr
 *         The string to measure.
 * @return
 *         The length of dstr.
 */
size_t dstrlen(const dchar *dstr);

/**
 * Reserve some capacity in a dynamic string.
 *
 * @param dstr
 *         The dynamic string to preallocate.
 * @param cap
 *         The new capacity for the string.
 * @return
 *         0 on success, -1 on failure.
 */
int dstreserve(dchar **dstr, size_t cap);

/**
 * Resize a dynamic string.
 *
 * @param dstr
 *         The dynamic string to resize.
 * @param len
 *         The new length for the dynamic string.
 * @return
 *         0 on success, -1 on failure.
 */
int dstresize(dchar **dstr, size_t len);

/**
 * Append to a dynamic string.
 *
 * @param dest
 *         The destination dynamic string.
 * @param src
 *         The string to append.
 * @return 0 on success, -1 on failure.
 */
int dstrcat(dchar **dest, const char *src);

/**
 * Append to a dynamic string.
 *
 * @param dest
 *         The destination dynamic string.
 * @param src
 *         The string to append.
 * @param n
 *         The maximum number of characters to take from src.
 * @return
 *         0 on success, -1 on failure.
 */
int dstrncat(dchar **dest, const char *src, size_t n);

/**
 * Append a dynamic string to another dynamic string.
 *
 * @param dest
 *         The destination dynamic string.
 * @param src
 *         The dynamic string to append.
 * @return
 *         0 on success, -1 on failure.
 */
int dstrdcat(dchar **dest, const dchar *src);

/**
 * Append to a dynamic string.
 *
 * @param dest
 *         The destination dynamic string.
 * @param src
 *         The string to append.
 * @param len
 *         The exact number of characters to take from src.
 * @return
 *         0 on success, -1 on failure.
 */
int dstrxcat(dchar **dest, const char *src, size_t len);

/**
 * Append a single character to a dynamic string.
 *
 * @param str
 *         The string to append to.
 * @param c
 *         The character to append.
 * @return
 *         0 on success, -1 on failure.
 */
int dstrapp(dchar **str, char c);

/**
 * Copy a string into a dynamic string.
 *
 * @param dest
 *         The destination dynamic string.
 * @param src
 *         The string to copy.
 * @returns
 *         0 on success, -1 on failure.
 */
int dstrcpy(dchar **dest, const char *str);

/**
 * Copy a dynamic string into another one.
 *
 * @param dest
 *         The destination dynamic string.
 * @param src
 *         The dynamic string to copy.
 * @returns
 *         0 on success, -1 on failure.
 */
int dstrdcpy(dchar **dest, const dchar *str);

/**
 * Copy a string into a dynamic string.
 *
 * @param dest
 *         The destination dynamic string.
 * @param src
 *         The dynamic string to copy.
 * @param n
 *         The maximum number of characters to take from src.
 * @returns
 *         0 on success, -1 on failure.
 */
int dstrncpy(dchar **dest, const char *str, size_t n);

/**
 * Copy a string into a dynamic string.
 *
 * @param dest
 *         The destination dynamic string.
 * @param src
 *         The dynamic string to copy.
 * @param len
 *         The exact number of characters to take from src.
 * @returns
 *         0 on success, -1 on failure.
 */
int dstrxcpy(dchar **dest, const char *str, size_t len);

/**
 * Create a dynamic string from a format string.
 *
 * @param format
 *         The format string to fill in.
 * @param ...
 *         Any arguments for the format string.
 * @return
 *         The created string, or NULL on failure.
 */
_printf(1, 2)
dchar *dstrprintf(const char *format, ...);

/**
 * Create a dynamic string from a format string and a va_list.
 *
 * @param format
 *         The format string to fill in.
 * @param args
 *         The arguments for the format string.
 * @return
 *         The created string, or NULL on failure.
 */
_printf(1, 0)
dchar *dstrvprintf(const char *format, va_list args);

/**
 * Format some text onto the end of a dynamic string.
 *
 * @param str
 *         The destination dynamic string.
 * @param format
 *         The format string to fill in.
 * @param ...
 *         Any arguments for the format string.
 * @return
 *         0 on success, -1 on failure.
 */
_printf(2, 3)
int dstrcatf(dchar **str, const char *format, ...);

/**
 * Format some text from a va_list onto the end of a dynamic string.
 *
 * @param str
 *         The destination dynamic string.
 * @param format
 *         The format string to fill in.
 * @param args
 *         The arguments for the format string.
 * @return
 *         0 on success, -1 on failure.
 */
_printf(2, 0)
int dstrvcatf(dchar **str, const char *format, va_list args);

/**
 * Concatenate while shell-escaping.
 *
 * @param dest
 *         The destination dynamic string.
 * @param str
 *         The string to escape.
 * @param flags
 *         Flags for wordesc().
 * @return
 *         0 on success, -1 on failure.
 */
int dstrescat(dchar **dest, const char *str, enum wesc_flags flags);

/**
 * Concatenate while shell-escaping.
 *
 * @param dest
 *         The destination dynamic string.
 * @param str
 *         The string to escape.
 * @param n
 *         The maximum length of the string.
 * @param flags
 *         Flags for wordesc().
 * @return
 *         0 on success, -1 on failure.
 */
int dstrnescat(dchar **dest, const char *str, size_t n, enum wesc_flags flags);

#endif // BFS_DSTRING_H
