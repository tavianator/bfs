// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

/**
 * Implementation of -printf/-fprintf.
 */

#ifndef BFS_PRINTF_H
#define BFS_PRINTF_H

#include "color.h"

struct BFTW;
struct bfs_ctx;
struct bfs_expr;

/**
 * A printf command, the result of parsing a single format string.
 */
struct bfs_printf;

/**
 * Parse a -printf format string.
 *
 * @ctx
 *         The bfs context.
 * @expr
 *         The expression to fill in.
 * @format
 *         The format string to parse.
 * @return
 *         0 on success, -1 on failure.
 */
int bfs_printf_parse(const struct bfs_ctx *ctx, struct bfs_expr *expr, const char *format);

/**
 * Evaluate a parsed format string.
 *
 * @cfile
 *         The CFILE to print to.
 * @format
 *         The parsed printf format.
 * @ftwbuf
 *         The bftw() data for the current file.
 * @return
 *         0 on success, -1 on failure.
 */
int bfs_printf(CFILE *cfile, const struct bfs_printf *format, const struct BFTW *ftwbuf);

/**
 * Free a parsed format string.
 */
void bfs_printf_free(struct bfs_printf *format);

#endif // BFS_PRINTF_H
