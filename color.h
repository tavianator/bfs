/*********************************************************************
 * bfs                                                               *
 * Copyright (C) 2015-2016 Tavian Barnes <tavianator@tavianator.com> *
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
#include <stdbool.h>
#include <stdio.h>

/**
 * A lookup table for colors.
 */
struct colors;

/**
 * Parse a color table.
 *
 * @param ls_color
 *         A color table in the LS_COLORS environment variable format.
 * @return The parsed color table.
 */
struct colors *parse_colors(const char *ls_colors);

/**
 * Free a color table.
 *
 * @param colors
 *         The color table to free.
 */
void free_colors(struct colors *colors);

/**
 * A file/stream with associated colors.
 */
typedef struct CFILE {
	/** The underlying file/stream. */
	FILE *file;
	/** The color table to use, if any. */
	const struct colors *colors;
	/** Whether to close the underlying stream. */
	bool close;
} CFILE;

/**
 * Open a file for colored output.
 *
 * @param path
 *         The path to the file to open.
 * @param colors
 *         The color table to use if file is a TTY.
 * @return A colored file stream.
 */
CFILE *cfopen(const char *path, const struct colors *colors);

/**
 * Make a colored copy of an open file.
 *
 * @param file
 *         The underlying file.
 * @param colors
 *         The color table to use if file is a TTY.
 * @return A colored wrapper around file.
 */
CFILE *cfdup(FILE *file, const struct colors *colors);

/**
 * Close a colored file.
 *
 * @param cfile
 *         The colored file to close.
 * @return 0 on success, -1 on failure.
 */
int cfclose(CFILE *cfile);

/**
 * Colored, formatted output.
 *
 * @param cfile
 *         The colored stream to print to.
 * @param format
 *         A printf()-style format string, supporting these format specifiers:
 *
 *         %%: A literal '%'
 *         %c: A single character
 *         %s: A string
 *         %P: A colored file path, from a const struct BFTW * argument
 *         %L: A colored link target, from a const struct BFTW * argument
 *         %{cc}: Change the color to 'cc'
 * @return 0 on success, -1 on failure.
 */
int cfprintf(CFILE *cfile, const char *format, ...);

#endif // BFS_COLOR_H
