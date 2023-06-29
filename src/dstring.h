// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

/**
 * A dynamic string library.
 */

#ifndef BFS_DSTRING_H
#define BFS_DSTRING_H

#include "config.h"
#include <stdarg.h>
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
 * Create a dynamic copy of a dynamic string.
 *
 * @param dstr
 *         The dynamic string to copy.
 */
char *dstrddup(const char *dstr);

/**
 * Create an exact-sized dynamic copy of a string.
 *
 * @param str
 *         The string to copy.
 * @param len
 *         The length of the string, which may include internal NUL bytes.
 */
char *dstrxdup(const char *str, size_t len);

/**
 * Get a dynamic string's length.
 *
 * @param dstr
 *         The string to measure.
 * @return
 *         The length of dstr.
 */
size_t dstrlen(const char *dstr);

/**
 * Reserve some capacity in a dynamic string.
 *
 * @param dstr
 *         The dynamic string to preallocate.
 * @param capacity
 *         The new capacity for the string.
 * @return
 *         0 on success, -1 on failure.
 */
int dstreserve(char **dstr, size_t capacity);

/**
 * Resize a dynamic string.
 *
 * @param dstr
 *         The dynamic string to resize.
 * @param length
 *         The new length for the dynamic string.
 * @return
 *         0 on success, -1 on failure.
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
 * @return
 *         0 on success, -1 on failure.
 */
int dstrncat(char **dest, const char *src, size_t n);

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
int dstrdcat(char **dest, const char *src);

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
int dstrxcat(char **dest, const char *src, size_t len);

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
int dstrapp(char **str, char c);

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
int dstrcpy(char **dest, const char *str);

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
int dstrdcpy(char **dest, const char *str);

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
int dstrncpy(char **dest, const char *str, size_t n);

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
int dstrxcpy(char **dest, const char *str, size_t len);

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
 * Create a dynamic string from a format string and a va_list.
 *
 * @param format
 *         The format string to fill in.
 * @param args
 *         The arguments for the format string.
 * @return
 *         The created string, or NULL on failure.
 */
BFS_FORMATTER(1, 0)
char *dstrvprintf(const char *format, va_list args);

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
BFS_FORMATTER(2, 3)
int dstrcatf(char **str, const char *format, ...);

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
BFS_FORMATTER(2, 0)
int dstrvcatf(char **str, const char *format, va_list args);

/**
 * Free a dynamic string.
 *
 * @param dstr
 *         The string to free.
 */
void dstrfree(char *dstr);

#endif // BFS_DSTRING_H
