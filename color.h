/****************************************************************************
 * bfs                                                                      *
 * Copyright (C) 2015-2017 Tavian Barnes <tavianator@tavianator.com>        *
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
 *         %d: An integer
 *         %g: A double
 *         %s: A string
 *         %zu: A size_t
 *         %m: strerror(errno)
 *         %P: A colored file path, from a const struct BFTW * argument
 *         %L: A colored link target, from a const struct BFTW * argument
 *         %{cc}: Change the color to 'cc'
 * @return 0 on success, -1 on failure.
 */
int cfprintf(CFILE *cfile, const char *format, ...);

#endif // BFS_COLOR_H
