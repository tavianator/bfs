/****************************************************************************
 * bfs                                                                      *
 * Copyright (C) 2016-2019 Tavian Barnes <tavianator@tavianator.com>        *
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
 * A dynamic string library.
 */

#ifndef BFS_DSTRING_H
#define BFS_DSTRING_H

#include "util.h"
#include <stddef.h>

/**
 * Allocate a dynamic string.
 *
 * @param capacity
 *         The initial capacity of the string.
 */
char *dstralloc(size_t capacity);

/**
 * Create a dynamic copy of a string.
 *
 * @param str
 *         The NUL-terminated string to copy.
 */
char *dstrdup(const char *str);

/**
 * Create a length-limited dynamic copy of a string.
 *
 * @param str
 *         The string to copy.
 * @param n
 *         The maximum number of characters to copy from str.
 */
char *dstrndup(const char *str, size_t n);

/**
 * Get a dynamic string's length.
 *
 * @param dstr
 *         The string to measure.
 * @return The length of dstr.
 */
size_t dstrlen(const char *dstr);

/**
 * Reserve some capacity in a dynamic string.
 *
 * @param dstr
 *         The dynamic string to preallocate.
 * @param capacity
 *         The new capacity for the string.
 * @return 0 on success, -1 on failure.
 */
int dstreserve(char **dstr, size_t capacity);

/**
 * Resize a dynamic string.
 *
 * @param dstr
 *         The dynamic string to resize.
 * @param length
 *         The new length for the dynamic string.
 * @return 0 on success, -1 on failure.
 */
int dstresize(char **dstr, size_t length);

/**
 * Append to a dynamic string.
 *
 * @param dest
 *         The destination dynamic string.
 * @param src
 *         The string to append.
 * @return 0 on success, -1 on failure.
 */
int dstrcat(char **dest, const char *src);

/**
 * Append to a dynamic string.
 *
 * @param dest
 *         The destination dynamic string.
 * @param src
 *         The string to append.
 * @param n
 *         The maximum number of characters to take from src.
 * @return 0 on success, -1 on failure.
 */
int dstrncat(char **dest, const char *src, size_t n);

/**
 * Append a single character to a dynamic string.
 *
 * @param str
 *         The string to append to.
 * @param c
 *         The character to append.
 * @return 0 on success, -1 on failure.
 */
int dstrapp(char **str, char c);

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
BFS_FORMATTER(1, 2)
char *dstrprintf(const char *format, ...);

/**
 * Free a dynamic string.
 *
 * @param dstr
 *         The string to free.
 */
void dstrfree(char *dstr);

#endif // BFS_DSTRING_H
