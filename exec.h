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

#ifndef BFS_EXEC_H
#define BFS_EXEC_H

#include "bftw.h"
#include "color.h"

struct cmdline;

/**
 * Flags for the -exec actions.
 */
enum bfs_exec_flags {
	/** Prompt the user before executing (-ok, -okdir). */
	BFS_EXEC_CONFIRM = 1 << 0,
	/** Run the command in the file's parent directory (-execdir, -okdir). */
	BFS_EXEC_CHDIR   = 1 << 1,
	/** Pass multiple files at once to the command (-exec ... {} +). */
	BFS_EXEC_MULTI   = 1 << 2,
	/** Print debugging information (-D exec). */
	BFS_EXEC_DEBUG   = 1 << 3,
};

/**
 * Buffer for a command line to be executed.
 */
struct bfs_exec {
	/** Flags for this exec buffer. */
	enum bfs_exec_flags flags;

	/** Command line template. */
	char **tmpl_argv;
	/** Command line template size. */
	size_t tmpl_argc;

	/** For BFS_EXEC_MULTI, the index of the placeholder argument. */
	size_t placeholder;

	/** The built command line. */
	char **argv;
	/** Number of command line arguments. */
	size_t argc;
	/** Capacity of argv. */
	size_t argv_cap;

	/** Current size of all arguments. */
	size_t arg_size;
	/** Maximum arg_size before E2BIG. */
	size_t arg_max;

	/** A file descriptor for the working directory, for BFS_EXEC_CHDIR. */
	int wd_fd;
	/** The path to the working directory, for BFS_EXEC_CHDIR. */
	char *wd_path;
	/** Length of the working directory path. */
	size_t wd_len;

	/** The ultimate return value for bfs_exec_finish(). */
	int ret;
};

/**
 * Parse an exec action.
 *
 * @param argv
 *         The (bfs) command line argument to parse.
 * @param flags
 *         Any flags for this exec action.
 * @param cmdline
 *         The command line.
 * @return The parsed exec action, or NULL on failure.
 */
struct bfs_exec *parse_bfs_exec(char **argv, enum bfs_exec_flags flags, const struct cmdline *cmdline);

/**
 * Execute the command for a file.
 *
 * @param execbuf
 *         The parsed exec action.
 * @param ftwbuf
 *         The bftw() data for the current file.
 * @return 0 if the command succeeded, -1 if it failed.  If the command could
 *         be executed, -1 is returned, and errno will be non-zero.  For
 *         BFS_EXEC_MULTI, errors will not be reported until bfs_exec_finish().
 */
int bfs_exec(struct bfs_exec *execbuf, const struct BFTW *ftwbuf);

/**
 * Finish executing any commands.
 *
 * @param execbuf
 *         The parsed exec action.
 * @return 0 on success, -1 if any errors were encountered.
 */
int bfs_exec_finish(struct bfs_exec *execbuf);

/**
 * Free a parsed exec action.
 */
void free_bfs_exec(struct bfs_exec *execbuf);

#endif // BFS_EXEC_H
