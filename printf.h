/****************************************************************************
 * bfs                                                                      *
 * Copyright (C) 2017 Tavian Barnes <tavianator@tavianator.com>             *
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

#ifndef BFS_PRINTF_H
#define BFS_PRINTF_H

#include "bftw.h"
#include "color.h"
#include <stdbool.h>
#include <stdio.h>

struct cmdline;
struct bfs_printf_directive;

/**
 * A printf command, the result of parsing a single format string.
 */
struct bfs_printf {
	/** The chain of printf directives. */
	struct bfs_printf_directive *directives;
	/** Whether the struct bfs_stat must be filled in. */
	bool needs_stat;
};

/**
 * Parse a -printf format string.
 *
 * @param format
 *         The format string to parse.
 * @param cmdline
 *         The command line.
 * @return The parsed printf command, or NULL on failure.
 */
struct bfs_printf *parse_bfs_printf(const char *format, struct cmdline *cmdline);

/**
 * Evaluate a parsed format string.
 *
 * @param file
 *         The FILE to print to.
 * @param command
 *         The parsed printf format.
 * @param ftwbuf
 *         The bftw() data for the current file.  If needs_stat is true, statbuf
 *         must be non-NULL.
 * @return 0 on success, -1 on failure.
 */
int bfs_printf(FILE *file, const struct bfs_printf *command, const struct BFTW *ftwbuf);

/**
 * Free a parsed format string.
 */
void free_bfs_printf(struct bfs_printf *command);

#endif // BFS_PRINTF_H
