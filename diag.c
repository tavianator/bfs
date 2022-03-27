/****************************************************************************
 * bfs                                                                      *
 * Copyright (C) 2019-2022 Tavian Barnes <tavianator@tavianator.com>        *
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

#include "diag.h"
#include "ctx.h"
#include "color.h"
#include "expr.h"
#include "util.h"
#include <assert.h>
#include <errno.h>
#include <stdarg.h>

void bfs_perror(const struct bfs_ctx *ctx, const char *str) {
	bfs_error(ctx, "%s: %m.\n", str);
}

void bfs_error(const struct bfs_ctx *ctx, const char *format, ...)  {
	va_list args;
	va_start(args, format);
	bfs_verror(ctx, format, args);
	va_end(args);
}

bool bfs_warning(const struct bfs_ctx *ctx, const char *format, ...)  {
	va_list args;
	va_start(args, format);
	bool ret = bfs_vwarning(ctx, format, args);
	va_end(args);
	return ret;
}

bool bfs_debug(const struct bfs_ctx *ctx, enum debug_flags flag, const char *format, ...)  {
	va_list args;
	va_start(args, format);
	bool ret = bfs_vdebug(ctx, flag, format, args);
	va_end(args);
	return ret;
}

void bfs_verror(const struct bfs_ctx *ctx, const char *format, va_list args) {
	int error = errno;

	bfs_error_prefix(ctx);

	errno = error;
	cvfprintf(ctx->cerr, format, args);
}

bool bfs_vwarning(const struct bfs_ctx *ctx, const char *format, va_list args) {
	int error = errno;

	if (bfs_warning_prefix(ctx)) {
		errno = error;
		cvfprintf(ctx->cerr, format, args);
		return true;
	} else {
		return false;
	}
}

bool bfs_vdebug(const struct bfs_ctx *ctx, enum debug_flags flag, const char *format, va_list args) {
	int error = errno;

	if (bfs_debug_prefix(ctx, flag)) {
		errno = error;
		cvfprintf(ctx->cerr, format, args);
		return true;
	} else {
		return false;
	}
}

void bfs_error_prefix(const struct bfs_ctx *ctx) {
	cfprintf(ctx->cerr, "${bld}%s:${rs} ${er}error:${rs} ", xbasename(ctx->argv[0]));
}

bool bfs_warning_prefix(const struct bfs_ctx *ctx) {
	if (ctx->warn) {
		cfprintf(ctx->cerr, "${bld}%s:${rs} ${wr}warning:${rs} ", xbasename(ctx->argv[0]));
		return true;
	} else {
		return false;
	}
}

bool bfs_debug_prefix(const struct bfs_ctx *ctx, enum debug_flags flag) {
	if (ctx->debug & flag) {
		cfprintf(ctx->cerr, "${bld}%s:${rs} ${cyn}-D %s${rs}: ", xbasename(ctx->argv[0]), debug_flag_name(flag));
		return true;
	} else {
		return false;
	}
}

/** Recursive part of highlight_expr(). */
static bool highlight_expr_recursive(const struct bfs_ctx *ctx, const struct bfs_expr *expr, bool *args) {
	if (!expr) {
		return false;
	}

	bool ret = false;

	if (!expr->synthetic) {
		size_t i = expr->argv - ctx->argv;
		for (size_t j = 0; j < expr->argc; ++j) {
			assert(i + j < ctx->argc);
			args[i + j] = true;
			ret = true;
		}
	}

	if (bfs_expr_has_children(expr)) {
		ret |= highlight_expr_recursive(ctx, expr->lhs, args);
		ret |= highlight_expr_recursive(ctx, expr->rhs, args);
	}

	return ret;
}

/** Highlight an expression in the command line. */
static bool highlight_expr(const struct bfs_ctx *ctx, const struct bfs_expr *expr, bool *args) {
	for (size_t i = 0; i < ctx->argc; ++i) {
		args[i] = false;
	}

	return highlight_expr_recursive(ctx, expr, args);
}

/** Print a highlighted portion of the command line. */
static void bfs_argv_diag(const struct bfs_ctx *ctx, const bool *args, bool warning) {
	if (warning) {
		bfs_warning_prefix(ctx);
	} else {
		bfs_error_prefix(ctx);
	}

	size_t max_argc = 0;
	for (size_t i = 0; i < ctx->argc; ++i) {
		if (i > 0) {
			cfprintf(ctx->cerr, " ");
		}

		if (args[i]) {
			max_argc = i + 1;
			cfprintf(ctx->cerr, "${bld}%s${rs}", ctx->argv[i]);
		} else {
			cfprintf(ctx->cerr, "%s", ctx->argv[i]);
		}
	}

	cfprintf(ctx->cerr, "\n");

	if (warning) {
		bfs_warning_prefix(ctx);
	} else {
		bfs_error_prefix(ctx);
	}

	for (size_t i = 0; i < max_argc; ++i) {
		if (i > 0) {
			if (args[i - 1] && args[i]) {
				cfprintf(ctx->cerr, "~");
			} else {
				cfprintf(ctx->cerr, " ");
			}
		}

		if (args[i] && (i == 0 || !args[i - 1])) {
			if (warning) {
				cfprintf(ctx->cerr, "${wr}");
			} else {
				cfprintf(ctx->cerr, "${er}");
			}
		}

		size_t len = xstrwidth(ctx->argv[i]);
		for (size_t j = 0; j < len; ++j) {
			if (args[i]) {
				cfprintf(ctx->cerr, "~");
			} else {
				cfprintf(ctx->cerr, " ");
			}
		}

		if (args[i] && (i + 1 >= max_argc || !args[i + 1])) {
			cfprintf(ctx->cerr, "${rs}");
		}
	}

	cfprintf(ctx->cerr, "\n");
}

void bfs_argv_error(const struct bfs_ctx *ctx, const bool *args) {
	bfs_argv_diag(ctx, args, false);
}

void bfs_expr_error(const struct bfs_ctx *ctx, const struct bfs_expr *expr) {
	bool args[ctx->argc];
	if (highlight_expr(ctx, expr, args)) {
		bfs_argv_error(ctx, args);
	}
}

bool bfs_argv_warning(const struct bfs_ctx *ctx, const bool *args) {
	if (!ctx->warn) {
		return false;
	}

	bfs_argv_diag(ctx, args, true);
	return true;
}

bool bfs_expr_warning(const struct bfs_ctx *ctx, const struct bfs_expr *expr) {
	bool args[ctx->argc];
	if (highlight_expr(ctx, expr, args)) {
		return bfs_argv_warning(ctx, args);
	}

	return false;
}
