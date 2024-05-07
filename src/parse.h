// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

/**
 * bfs command line parsing.
 */

#ifndef BFS_PARSE_H
#define BFS_PARSE_H

/**
 * Parse the command line.
 *
 * @param argc
 *         The number of arguments.
 * @param argv
 *         The arguments to parse.
 * @return
 *         A new bfs context, or NULL on failure.
 */
struct bfs_ctx *bfs_parse_cmdline(int argc, char *argv[]);

#endif // BFS_PARSE_H
