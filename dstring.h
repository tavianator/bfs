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

#ifndef BFS_DSTRING_H
#define BFS_DSTRING_H

#include <stddef.h>

/**
 * Allocate a dynamic string.
 *
 * @param capacity
 *         The initial capacity of the string.
 */
char *dstralloc(size_t capacity);

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
 * Append to a dynamic string at a particular offset.
 *
 * @param dest
 *         The destination dynamic string.
 * @param pos
 *         The offset at which to append.
 * @param src
 *         The string to append.
 * @return 0 on success, -1 on failure.
 */
int dstrcatat(char **dest, size_t pos, const char *src);

/**
 * Free a dynamic string.
 *
 * @param dstr
 *         The string to free.
 */
void dstrfree(char *dstr);

#endif // BFS_DSTRING_H
