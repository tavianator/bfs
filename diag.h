/****************************************************************************
 * bfs                                                                      *
 * Copyright (C) 2019 Tavian Barnes <tavianator@tavianator.com>             *
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
 * Formatters for diagnostic messages.
 */

#ifndef BFS_DIAG_H
#define BFS_DIAG_H

#include "ctx.h"
#include "util.h"
#include <stdarg.h>
#include <stdbool.h>

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
void bfs_verror(const struct bfs_ctx *ctx, const char *format, va_list args);

/**
 * bfs_warning() variant that takes a va_list.
 */
bool bfs_vwarning(const struct bfs_ctx *ctx, const char *format, va_list args);

/**
 * bfs_debug() variant that takes a va_list.
 */
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

#endif // BFS_DIAG_H
