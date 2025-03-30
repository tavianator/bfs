// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

/**
 * Utilities for colored output on ANSI terminals.
 */

#ifndef BFS_COLOR_H
#define BFS_COLOR_H

#include "bfs.h"
#include "dstring.h"

#include <stdio.h>

/**
 * A color scheme.
 */
struct colors;

/**
 * Parse the color table from the environment.
 */
struct colors *parse_colors(void);

/**
 * Check if stat() info is required to color a file correctly.
 */
bool colors_need_stat(const struct colors *colors);

/**
 * Free a color table.
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
	dchar *buffer;
	/** Cached file descriptor number. */
	int fd;
	/** Whether the next ${rs} is actually necessary. */
	bool need_reset;
	/** Whether to close the underlying stream. */
	bool close;
} CFILE;

/**
 * Wrap an existing file into a colored stream.
 *
 * @file
 *         The underlying file.
 * @colors
 *         The color table to use if file is a TTY.
 * @close
 *         Whether to close the underlying stream when this stream is closed.
 * @return
 *         A colored wrapper around file.
 */
CFILE *cfwrap(FILE *file, const struct colors *colors, bool close);

/**
 * Close a colored file.
 *
 * @cfile
 *         The colored file to close.
 * @return
 *         0 on success, -1 on failure.
 */
int cfclose(CFILE *cfile);

/**
 * Colored, formatted output.
 *
 * @cfile
 *         The colored stream to print to.
 * @format
 *         A printf()-style format string, supporting these format specifiers:
 *
 *         %c: A single character
 *         %d: An integer
 *         %g: A double
 *         %s: A string
 *         %zu: A size_t
 *         %pq: A shell-escaped string, like bash's printf %q
 *         %pQ: A TTY-escaped string.
 *         %pF: A colored file name, from a const struct BFTW * argument
 *         %pP: A colored file path, from a const struct BFTW * argument
 *         %pL: A colored link target, from a const struct BFTW * argument
 *         %pe: Dump a const struct bfs_expr *, for debugging.
 *         %pE: Dump a const struct bfs_expr * in verbose form, for debugging.
 *         %px: Print a const struct bfs_expr * with syntax highlighting.
 *         %pX: Print the name of a const struct bfs_expr *, without arguments.
 *         %%: A literal '%'
 *         ${cc}: Change the color to 'cc'
 *         $$: A literal '$'
 * @return
 *         0 on success, -1 on failure.
 */
_printf(2, 3)
int cfprintf(CFILE *cfile, const char *format, ...);

/**
 * cfprintf() variant that takes a va_list.
 */
_printf(2, 0)
int cvfprintf(CFILE *cfile, const char *format, va_list args);

/**
 * Reset the TTY state when terminating abnormally (async-signal-safe).
 */
int cfreset(CFILE *cfile);

#endif // BFS_COLOR_H
