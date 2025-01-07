// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

/**
 * Diagnostic messages.
 */

#ifndef BFS_DIAG_H
#define BFS_DIAG_H

#include "bfs.h"
#include "bfstd.h"

#include <stdarg.h>

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
#if BFS_HAS_COMPOUND_LITERAL_STORAGE
#  define bfs_location() (&(static const struct bfs_loc)BFS_LOC_INIT)
#else
#  define bfs_location() (&(const struct bfs_loc)BFS_LOC_INIT)
#endif

/**
 * Print a low-level diagnostic message to standard error, formatted like
 *
 *     bfs: func@src/file.c:0: Message
 */
_printf(2, 3)
void bfs_diagf(const struct bfs_loc *loc, const char *format, ...);

/**
 * Unconditional diagnostic message.
 */
#define bfs_diag(...) bfs_diagf(bfs_location(), __VA_ARGS__)

/**
 * Print a diagnostic message including the last error.
 */
#define bfs_ediag(...) \
	bfs_ediag_("" __VA_ARGS__, errstr())

#define bfs_ediag_(format, ...) \
	bfs_diag(sizeof(format) > 1 ? format ": %s" : "%s", __VA_ARGS__)

/**
 * Print a message to standard error and abort.
 */
_cold
_printf(2, 3)
_noreturn
void bfs_abortf(const struct bfs_loc *loc, const char *format, ...);

/**
 * Unconditional abort with a message.
 */
#define bfs_abort(...) \
	bfs_abortf(bfs_location(), __VA_ARGS__)

/**
 * Abort with a message including the last error.
 */
#define bfs_eabort(...) \
	bfs_eabort_("" __VA_ARGS__, errstr())

#define bfs_eabort_(format, ...) \
	bfs_abort(sizeof(format) > 1 ? format ": %s" : "%s", __VA_ARGS__)

/**
 * Abort in debug builds; no-op in release builds.
 */
#ifdef NDEBUG
#  define bfs_bug(...) ((void)0)
#  define bfs_ebug(...) ((void)0)
#else
#  define bfs_bug bfs_abort
#  define bfs_ebug bfs_eabort
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
 * Unconditional assert, including the last error.
 */
#define bfs_everify(...) \
	bfs_everify_(#__VA_ARGS__, __VA_ARGS__, "", errstr())

#define bfs_everify_(str, cond, format, ...) \
	((cond) ? (void)0 : bfs_abort( \
		sizeof(format) > 1 \
			? "%.0s" format "%s: %s" \
			: "Assertion failed: `%s`: %s", \
		str, __VA_ARGS__))

/**
 * Assert in debug builds; no-op in release builds.
 */
#ifdef NDEBUG
#  define bfs_assert(...) ((void)0)
#  define bfs_eassert(...) ((void)0)
#else
#  define bfs_assert bfs_verify
#  define bfs_eassert bfs_everify
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
_cold
void bfs_perror(const struct bfs_ctx *ctx, const char *str);

/**
 * Shorthand for printing error messages.
 */
_cold
_printf(2, 3)
void bfs_error(const struct bfs_ctx *ctx, const char *format, ...);

/**
 * Shorthand for printing warning messages.
 *
 * @return Whether a warning was printed.
 */
_cold
_printf(2, 3)
bool bfs_warning(const struct bfs_ctx *ctx, const char *format, ...);

/**
 * Shorthand for printing debug messages.
 *
 * @return Whether a debug message was printed.
 */
_cold
_printf(3, 4)
bool bfs_debug(const struct bfs_ctx *ctx, enum debug_flags flag, const char *format, ...);

/**
 * bfs_error() variant that takes a va_list.
 */
_cold
_printf(2, 0)
void bfs_verror(const struct bfs_ctx *ctx, const char *format, va_list args);

/**
 * bfs_warning() variant that takes a va_list.
 */
_cold
_printf(2, 0)
bool bfs_vwarning(const struct bfs_ctx *ctx, const char *format, va_list args);

/**
 * bfs_debug() variant that takes a va_list.
 */
_cold
_printf(3, 0)
bool bfs_vdebug(const struct bfs_ctx *ctx, enum debug_flags flag, const char *format, va_list args);

/**
 * Print the error message prefix.
 */
_cold
void bfs_error_prefix(const struct bfs_ctx *ctx);

/**
 * Print the warning message prefix.
 */
_cold
bool bfs_warning_prefix(const struct bfs_ctx *ctx);

/**
 * Print the debug message prefix.
 */
_cold
bool bfs_debug_prefix(const struct bfs_ctx *ctx, enum debug_flags flag);

/**
 * Highlight parts of the command line in an error message.
 */
_cold
void bfs_argv_error(const struct bfs_ctx *ctx, const bool args[]);

/**
 * Highlight parts of an expression in an error message.
 */
_cold
void bfs_expr_error(const struct bfs_ctx *ctx, const struct bfs_expr *expr);

/**
 * Highlight parts of the command line in a warning message.
 */
_cold
bool bfs_argv_warning(const struct bfs_ctx *ctx, const bool args[]);

/**
 * Highlight parts of an expression in a warning message.
 */
_cold
bool bfs_expr_warning(const struct bfs_ctx *ctx, const struct bfs_expr *expr);

#endif // BFS_DIAG_H
