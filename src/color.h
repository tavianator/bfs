// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

/**
 * Utilities for colored output on ANSI terminals.
 */

#ifndef BFS_COLOR_H
#define BFS_COLOR_H

#include "config.h"
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>

/**
 * A color scheme.
 */
struct colors;

/**
 * Parse a color table.
 *
 * @return The parsed color table.
 */
struct colors *parse_colors(void);

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
 *         %pF: A colored file name, from a const struct BFTW * argument
 *         %pP: A colored file path, from a const struct BFTW * argument
 *         %pL: A colored link target, from a const struct BFTW * argument
 *         %pe: Dump a const struct bfs_expr *, for debugging.
 *         %pE: Dump a const struct bfs_expr * in verbose form, for debugging.
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
BFS_FORMATTER(2, 0)
int cvfprintf(CFILE *cfile, const char *format, va_list args);

#endif // BFS_COLOR_H
