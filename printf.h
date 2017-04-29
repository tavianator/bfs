/*********************************************************************
 * bfs                                                               *
 * Copyright (C) 2017 Tavian Barnes <tavianator@tavianator.com>      *
 *                                                                   *
 * This program is free software. It comes without any warranty, to  *
 * the extent permitted by applicable law. You can redistribute it   *
 * and/or modify it under the terms of the Do What The Fuck You Want *
 * To Public License, Version 2, as published by Sam Hocevar. See    *
 * the COPYING file or http://www.wtfpl.net/ for more details.       *
 *********************************************************************/

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
	/** Whether the struct stat must be filled in. */
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
