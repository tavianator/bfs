/****************************************************************************
 * bfs                                                                      *
 * Copyright (C) 2015-2018 Tavian Barnes <tavianator@tavianator.com>        *
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
 * Representation of the parsed command line.
 */

#ifndef BFS_CMDLINE_H
#define BFS_CMDLINE_H

#include "color.h"
#include "trie.h"

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
	/** All debug flags. */
	DEBUG_ALL    = (1 << 7) - 1,
};

/**
 * The parsed command line.
 */
struct cmdline {
	/** The unparsed command line arguments. */
	char **argv;

	/** The root paths. */
	const char **paths;

	/** Color data. */
	struct colors *colors;
	/** Colored stdout. */
	CFILE *cout;
	/** Colored stderr. */
	CFILE *cerr;

	/** User table. */
	struct bfs_users *users;
	/** The error that occurred parsing the user table, if any. */
	int users_error;
	/** Group table. */
	struct bfs_groups *groups;
	/** The error that occurred parsing the group table, if any. */
	int groups_error;

	/** Table of mounted file systems. */
	struct bfs_mtab *mtab;
	/** The error that occurred parsing the mount table, if any. */
	int mtab_error;

	/** -mindepth option. */
	int mindepth;
	/** -maxdepth option. */
	int maxdepth;

	/** bftw() flags. */
	enum bftw_flags flags;
	/** bftw() search strategy. */
	enum bftw_strategy strategy;

	/** Optimization level (-O). */
	int optlevel;
	/** Debugging flags (-D). */
	enum debug_flags debug;
	/** Whether to ignore deletions that race with bfs (-ignore_readdir_race). */
	bool ignore_races;
	/** Whether to only return unique files (-unique). */
	bool unique;
	/** Whether to print warnings (-warn/-nowarn). */
	bool warn;
	/** Whether to only handle paths with xargs-safe characters (-X). */
	bool xargs_safe;

	/** The command line expression. */
	struct expr *expr;

	/** All the open files owned by the command line. */
	struct trie open_files;
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
