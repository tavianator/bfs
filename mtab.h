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

#ifndef BFS_MTAB_H
#define BFS_MTAB_H

#include "stat.h"

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
 *         The bfs_stat() buffer for the file in question.
 * @return The type of file system containing this file, "unknown" if not known,
 *         or NULL on error.
 */
const char *bfs_fstype(const struct bfs_mtab *mtab, const struct bfs_stat *statbuf);

/**
 * Free a mount table.
 */
void free_bfs_mtab(struct bfs_mtab *mtab);

#endif // BFS_MTAB_H
