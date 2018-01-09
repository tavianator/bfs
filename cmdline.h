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

#ifndef BFS_CMDLINE_H
#define BFS_CMDLINE_H

#include "color.h"

/**
 * Various debugging flags.
 */
enum debug_flags {
	/** Print cost estimates. */
	DEBUG_COST   = 1 << 0,
	/** Print executed command details. */
	DEBUG_EXEC   = 1 << 1,
	/** Print optimization details. */
	DEBUG_OPT    = 1 << 2,
	/** Print rate information. */
	DEBUG_RATES  = 1 << 3,
	/** Trace the filesystem traversal. */
	DEBUG_SEARCH = 1 << 4,
	/** Trace all stat() calls. */
	DEBUG_STAT   = 1 << 5,
	/** Print the parse tree. */
	DEBUG_TREE   = 1 << 6,
};

/**
 * A root path to explore.
 */
struct root {
	/** The root path itself. */
	const char *path;
	/** The next path in the list. */
	struct root *next;
};

/**
 * An open file for the command line.
 */
struct open_file;

/**
 * The parsed command line.
 */
struct cmdline {
	/** The unparsed command line arguments. */
	char **argv;

	/** The list of root paths. */
	struct root *roots;

	/** Color data. */
	struct colors *colors;
	/** Colored stdout. */
	CFILE *cout;
	/** Colored stderr. */
	CFILE *cerr;

	/** Table of mounted file systems. */
	struct bfs_mtab *mtab;

	/** -mindepth option. */
	int mindepth;
	/** -maxdepth option. */
	int maxdepth;

	/** bftw() flags. */
	enum bftw_flags flags;

	/** Optimization level. */
	int optlevel;
	/** Debugging flags. */
	enum debug_flags debug;
	/** Whether to only handle paths with xargs-safe characters. */
	bool xargs_safe;
	/** Whether to ignore deletions that race with bfs. */
	bool ignore_races;

	/** The command line expression. */
	struct expr *expr;

	/** All the open files owned by the command line. */
	struct open_file *open_files;
	/** The number of open files owned by the command line. */
	int nopen_files;
};

/**
 * Parse the command line.
 */
struct cmdline *parse_cmdline(int argc, char *argv[]);

/**
 * Dump the parsed command line.
 */
void dump_cmdline(const struct cmdline *cmdline, bool verbose);

/**
 * Optimize the parsed command line.
 *
 * @return 0 if successful, -1 on error.
 */
int optimize_cmdline(struct cmdline *cmdline);

/**
 * Evaluate the command line.
 */
int eval_cmdline(const struct cmdline *cmdline);

/**
 * Free the parsed command line.
 *
 * @return 0 if successful, -1 on error.
 */
int free_cmdline(struct cmdline *cmdline);

#endif // BFS_CMDLINE_H
