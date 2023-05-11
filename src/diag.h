// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

/**
 * Formatters for diagnostic messages.
 */

#ifndef BFS_DIAG_H
#define BFS_DIAG_H

#include "ctx.h"
#include "config.h"
#include <stdarg.h>

struct bfs_expr;

/**
 * Like perror(), but decorated like bfs_error().
 */
void bfs_perror(const struct bfs_ctx *ctx, const char *str);

/**
 * Shorthand for printing error messages.
 */
BFS_FORMATTER(2, 3)
void bfs_error(const struct bfs_ctx *ctx, const char *format, ...);

/**
 * Shorthand for printing warning messages.
 *
 * @return Whether a warning was printed.
 */
BFS_FORMATTER(2, 3)
bool bfs_warning(const struct bfs_ctx *ctx, const char *format, ...);

/**
 * Shorthand for printing debug messages.
 *
 * @return Whether a debug message was printed.
 */
BFS_FORMATTER(3, 4)
bool bfs_debug(const struct bfs_ctx *ctx, enum debug_flags flag, const char *format, ...);

/**
 * bfs_error() variant that takes a va_list.
 */
BFS_FORMATTER(2, 0)
void bfs_verror(const struct bfs_ctx *ctx, const char *format, va_list args);

/**
 * bfs_warning() variant that takes a va_list.
 */
BFS_FORMATTER(2, 0)
bool bfs_vwarning(const struct bfs_ctx *ctx, const char *format, va_list args);

/**
 * bfs_debug() variant that takes a va_list.
 */
BFS_FORMATTER(3, 0)
bool bfs_vdebug(const struct bfs_ctx *ctx, enum debug_flags flag, const char *format, va_list args);

/**
 * Print the error message prefix.
 */
void bfs_error_prefix(const struct bfs_ctx *ctx);

/**
 * Print the warning message prefix.
 */
bool bfs_warning_prefix(const struct bfs_ctx *ctx);

/**
 * Print the debug message prefix.
 */
bool bfs_debug_prefix(const struct bfs_ctx *ctx, enum debug_flags flag);

/**
 * Highlight parts of the command line in an error message.
 */
void bfs_argv_error(const struct bfs_ctx *ctx, const bool *args);

/**
 * Highlight parts of an expression in an error message.
 */
void bfs_expr_error(const struct bfs_ctx *ctx, const struct bfs_expr *expr);

/**
 * Highlight parts of the command line in a warning message.
 */
bool bfs_argv_warning(const struct bfs_ctx *ctx, const bool *args);

/**
 * Highlight parts of an expression in a warning message.
 */
bool bfs_expr_warning(const struct bfs_ctx *ctx, const struct bfs_expr *expr);

#endif // BFS_DIAG_H
