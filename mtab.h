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

#ifndef BFS_MTAB_H
#define BFS_MTAB_H

#include <sys/stat.h>

/**
 * A file system mount table.
 */
struct bfs_mtab;

/**
 * Parse the mount table.
 *
 * @return The parsed mount table, or NULL on error.
 */
struct bfs_mtab *parse_bfs_mtab(void);

/**
 * Determine the file system type that a file is on.
 *
 * @param mtab
 *         The current mount table.
 * @param statbuf
 *         The stat() buffer for the file in question.
 * @return The type of file system containing this file, "unknown" if not known,
 *         or NULL on error.
 */
const char *bfs_fstype(const struct bfs_mtab *mtab, const struct stat *statbuf);

/**
 * Free a mount table.
 */
void free_bfs_mtab(struct bfs_mtab *mtab);

#endif // BFS_MTAB_H
