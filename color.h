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
#include <sys/stat.h>

/**
 * A lookup table for colors.
 */
typedef struct color_table color_table;

/**
 * Parse a color table.
 *
 * @param ls_color
 *         A color table in the LS_COLORS environment variable format.
 * @return The parsed color table.
 */
color_table *parse_colors(char *ls_colors);

/**
 * Pretty-print a file path.
 *
 * @param colors
 *         The color table to use.
 * @param fpath
 *         The file path to print.
 * @param sb
 *         A stat() buffer for fpath.
 */
void pretty_print(const color_table *colors, const char *fpath, const struct stat *sb);

/**
 * Pretty-print an error.
 *
 * @param colors
 *         The color table to use.
 * @param fpath
 *         The file path in error.
 * @param ftwbuf
 *         The bftw() data for fpath.
 */
void print_error(const color_table *colors, const char *fpath, const struct BFTW *ftwbuf);

/**
 * Free a color table.
 *
 * @param colors
 *         The color table to free.
 */
void free_colors(color_table *colors);

#endif // BFS_COLOR_H
