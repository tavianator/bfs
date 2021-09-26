/****************************************************************************
 * bfs                                                                      *
 * Copyright (C) 2015-2021 Tavian Barnes <tavianator@tavianator.com>        *
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
 * Utilities for colored output on ANSI terminals.
 */

#ifndef BFS_COLOR_H
#define BFS_COLOR_H

#include "util.h"
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>

/**
 * A lookup table for colors.
 */
struct colors;

/**
 * Parse a color table.
 *
 * @param ls_colors
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
	/** A buffer for colored formatting. */
	char *buffer;
	/** Whether to close the underlying stream. */
	bool close;
} CFILE;

/**
 * Wrap an existing file into a colored stream.
 *
 * @param file
 *         The underlying file.
 * @param colors
 *         The color table to use if file is a TTY.
 * @param close
 *         Whether to close the underlying stream when this stream is closed.
 * @return
 *         A colored wrapper around file.
 */
CFILE *cfwrap(FILE *file, const struct colors *colors, bool close);

/**
 * Close a colored file.
 *
 * @param cfile
 *         The colored file to close.
 * @return
 *         0 on success, -1 on failure.
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
 *         %c: A single character
 *         %d: An integer
 *         %g: A double
 *         %s: A string
 *         %zu: A size_t
 *         %m: strerror(errno)
 *         %pP: A colored file path, from a const struct BFTW * argument
 *         %pL: A colored link target, from a const struct BFTW * argument
 *         %pe: Dump a const struct expr *, for debugging.
 *         %pE: Dump a const struct expr * in verbose form, for debugging.
 *         %%: A literal '%'
 *         ${cc}: Change the color to 'cc'
 *         $$: A literal '$'
 * @return
 *         0 on success, -1 on failure.
 */
BFS_FORMATTER(2, 3)
int cfprintf(CFILE *cfile, const char *format, ...);

/**
 * cfprintf() variant that takes a va_list.
 */
int cvfprintf(CFILE *cfile, const char *format, va_list args);

#endif // BFS_COLOR_H
