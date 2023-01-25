// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

/**
 * Optimization.
 */

#ifndef BFS_OPT_H
#define BFS_OPT_H

struct bfs_ctx;

/**
 * Apply optimizations to the command line.
 *
 * @param ctx
 *         The bfs context to optimize.
 * @return
 *         0 if successful, -1 on error.
 */
int bfs_optimize(struct bfs_ctx *ctx);

#endif // BFS_OPT_H

