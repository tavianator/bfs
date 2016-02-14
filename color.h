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

#ifndef BFS_COLOR_H
#define BFS_COLOR_H

#include "bftw.h"

/**
 * A lookup table for colors.
 */
struct color_table;

/**
 * Parse a color table.
 *
 * @param ls_color
 *         A color table in the LS_COLORS environment variable format.
 * @return The parsed color table.
 */
struct color_table *parse_colors(const char *ls_colors);

/**
 * Pretty-print a file path.
 *
 * @param colors
 *         The color table to use.
 * @param ftwbuf
 *         The bftw() data for the current path.
 */
void pretty_print(const struct color_table *colors, const struct BFTW *ftwbuf);

#if __GNUC__
#	define BFS_PRINTF_ATTRIBUTE(f, v) __attribute__((format(printf, f, v)))
#else
#	define BFS_PRINTF_ATTRIBUTE(f, v)
#endif

/**
 * Pretty-print a warning message.
 *
 * @param colors
 *         The color table to use.
 * @param format
 *         The format string.
 * @param ...
 *         The format string's arguments.
 */
void pretty_warning(const struct color_table *colors, const char *format, ...) BFS_PRINTF_ATTRIBUTE(2, 3);

/**
 * Pretty-print an error message.
 *
 * @param colors
 *         The color table to use.
 * @param format
 *         The format string.
 * @param ...
 *         The format string's arguments.
 */
void pretty_error(const struct color_table *colors, const char *format, ...) BFS_PRINTF_ATTRIBUTE(2, 3);

/**
 * Free a color table.
 *
 * @param colors
 *         The color table to free.
 */
void free_colors(struct color_table *colors);

#endif // BFS_COLOR_H
