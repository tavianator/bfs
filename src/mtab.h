// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

/**
 * A facade over platform-specific APIs for enumerating mounted filesystems.
 */

#ifndef BFS_MTAB_H
#define BFS_MTAB_H

#include "config.h"

struct bfs_stat;

/**
 * A file system mount table.
 */
struct bfs_mtab;

/**
 * Parse the mount table.
 *
 * @return
 *         The parsed mount table, or NULL on error.
 */
struct bfs_mtab *bfs_mtab_parse(void);

/**
 * Determine the file system type that a file is on.
 *
 * @param mtab
 *         The current mount table.
 * @param statbuf
 *         The bfs_stat() buffer for the file in question.
 * @return
 *         The type of file system containing this file, "unknown" if not known,
 *         or NULL on error.
 */
const char *bfs_fstype(const struct bfs_mtab *mtab, const struct bfs_stat *statbuf);

/**
 * Check if a file could be a mount point.
 *
 * @param mtab
 *         The current mount table.
 * @param name
 *         The name of the file to check.
 * @return
 *         Whether the named file could be a mount point.
 */
bool bfs_might_be_mount(const struct bfs_mtab *mtab, const char *name);

/**
 * Free a mount table.
 */
void bfs_mtab_free(struct bfs_mtab *mtab);

#endif // BFS_MTAB_H
