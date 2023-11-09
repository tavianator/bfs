// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

/**
 * Diagnostic messages.
 */

#ifndef BFS_DIAG_H
#define BFS_DIAG_H

#include "config.h"
#include <stdarg.h>

/**
 * static_assert() with an optional second argument.
 */
#if __STDC_VERSION__ >= 202311L
#  define bfs_static_assert static_assert
#else
#  define bfs_static_assert(...) bfs_static_assert_(__VA_ARGS__, #__VA_ARGS__, )
#  define bfs_static_assert_(expr, msg, ...) _Static_assert(expr, msg)
#endif

/**
 * A source code location.
 */
struct bfs_loc {
	const char *file;
	int line;
	const char *func;
};

#define BFS_LOC_INIT { .file = __FILE__, .line = __LINE__, .func = __func__ }

/**
 * Get the current source code location.
 */
#if __STDC_VERSION__ >= 202311L
#  define bfs_location() (&(static const struct bfs_loc)BFS_LOC_INIT)
#else
#  define bfs_location() (&(const struct bfs_loc)BFS_LOC_INIT)
#endif

/**
 * Print a message to standard error and abort.
 */
attr_format(2, 3)
noreturn void bfs_abortf(const struct bfs_loc *loc, const char *format, ...);

/**
 * Unconditional abort with a message.
 */
#define bfs_abort(...) bfs_abortf(bfs_location(), __VA_ARGS__)

/**
 * Abort in debug builds; no-op in release builds.
 */
#ifdef NDEBUG
#  define bfs_bug(...) ((void)0)
#else
#  define bfs_bug bfs_abort
#endif

/**
 * Unconditional assert.
 */
#define bfs_verify(...) \
	bfs_verify_(#__VA_ARGS__, __VA_ARGS__, "", "")

#define bfs_verify_(str, cond, format, ...) \
	((cond) ? (void)0 : bfs_abort( \
		sizeof(format) > 1 \
			? "%.0s" format "%s%s" \
			: "Assertion failed: `%s`%s", \
		str, __VA_ARGS__))

/**
 * Assert in debug builds; no-op in release builds.
 */
#ifdef NDEBUG
#  define bfs_assert(...) ((void)0)
#else
#  define bfs_assert bfs_verify
#endif

struct bfs_ctx;
struct bfs_expr;

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
 * Convert a debug flag to a string.
 */
const char *debug_flag_name(enum debug_flags flag);

/**
 * Like perror(), but decorated like bfs_error().
 */
void bfs_perror(const struct bfs_ctx *ctx, const char *str);

/**
 * Shorthand for printing error messages.
 */
attr_format(2, 3)
void bfs_error(const struct bfs_ctx *ctx, const char *format, ...);

/**
 * Shorthand for printing warning messages.
 *
 * @return Whether a warning was printed.
 */
attr_format(2, 3)
bool bfs_warning(const struct bfs_ctx *ctx, const char *format, ...);

/**
 * Shorthand for printing debug messages.
 *
 * @return Whether a debug message was printed.
 */
attr_format(3, 4)
bool bfs_debug(const struct bfs_ctx *ctx, enum debug_flags flag, const char *format, ...);

/**
 * bfs_error() variant that takes a va_list.
 */
attr_format(2, 0)
void bfs_verror(const struct bfs_ctx *ctx, const char *format, va_list args);

/**
 * bfs_warning() variant that takes a va_list.
 */
attr_format(2, 0)
bool bfs_vwarning(const struct bfs_ctx *ctx, const char *format, va_list args);

/**
 * bfs_debug() variant that takes a va_list.
 */
attr_format(3, 0)
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
void bfs_argv_error(const struct bfs_ctx *ctx, const bool args[]);

/**
 * Highlight parts of an expression in an error message.
 */
void bfs_expr_error(const struct bfs_ctx *ctx, const struct bfs_expr *expr);

/**
 * Highlight parts of the command line in a warning message.
 */
bool bfs_argv_warning(const struct bfs_ctx *ctx, const bool args[]);

/**
 * Highlight parts of an expression in a warning message.
 */
bool bfs_expr_warning(const struct bfs_ctx *ctx, const struct bfs_expr *expr);

#endif // BFS_DIAG_H
