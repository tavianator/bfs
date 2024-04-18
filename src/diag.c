// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include "prelude.h"
#include "diag.h"
#include "alloc.h"
#include "bfstd.h"
#include "color.h"
#include "ctx.h"
#include "dstring.h"
#include "expr.h"
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

/** bfs_diagf() implementation. */
attr(printf(2, 0))
static void bfs_vdiagf(const struct bfs_loc *loc, const char *format, va_list args) {
	fprintf(stderr, "%s: %s@%s:%d: ", xgetprogname(), loc->func, loc->file, loc->line);
	vfprintf(stderr, format, args);
	fprintf(stderr, "\n");
}

void bfs_diagf(const struct bfs_loc *loc, const char *format, ...) {
	va_list args;
	va_start(args, format);
	bfs_vdiagf(loc, format, args);
	va_end(args);
}

noreturn void bfs_abortf(const struct bfs_loc *loc, const char *format, ...) {
	va_list args;
	va_start(args, format);
	bfs_vdiagf(loc, format, args);
	va_end(args);

	abort();
}

const char *debug_flag_name(enum debug_flags flag) {
	switch (flag) {
	case DEBUG_COST:
		return "cost";
	case DEBUG_EXEC:
		return "exec";
	case DEBUG_OPT:
		return "opt";
	case DEBUG_RATES:
		return "rates";
	case DEBUG_SEARCH:
		return "search";
	case DEBUG_STAT:
		return "stat";
	case DEBUG_TREE:
		return "tree";

	case DEBUG_ALL:
		break;
	}

	bfs_bug("Unrecognized debug flag");
	return "???";
}

void bfs_perror(const struct bfs_ctx *ctx, const char *str) {
	bfs_error(ctx, "%s: %m.\n", str);
}

void bfs_error(const struct bfs_ctx *ctx, const char *format, ...) {
	va_list args;
	va_start(args, format);
	bfs_verror(ctx, format, args);
	va_end(args);
}

bool bfs_warning(const struct bfs_ctx *ctx, const char *format, ...) {
	va_list args;
	va_start(args, format);
	bool ret = bfs_vwarning(ctx, format, args);
	va_end(args);
	return ret;
}

bool bfs_debug(const struct bfs_ctx *ctx, enum debug_flags flag, const char *format, ...) {
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

/** Get the command name without any leading directories. */
static const char *bfs_cmd(const struct bfs_ctx *ctx) {
	return ctx->argv[0] + xbaseoff(ctx->argv[0]);
}

void bfs_error_prefix(const struct bfs_ctx *ctx) {
	cfprintf(ctx->cerr, "${bld}%s:${rs} ${err}error:${rs} ", bfs_cmd(ctx));
}

bool bfs_warning_prefix(const struct bfs_ctx *ctx) {
	if (ctx->warn) {
		cfprintf(ctx->cerr, "${bld}%s:${rs} ${wrn}warning:${rs} ", bfs_cmd(ctx));
		return true;
	} else {
		return false;
	}
}

bool bfs_debug_prefix(const struct bfs_ctx *ctx, enum debug_flags flag) {
	if (ctx->debug & flag) {
		cfprintf(ctx->cerr, "${bld}%s:${rs} ${cyn}-D %s${rs}: ", bfs_cmd(ctx), debug_flag_name(flag));
		return true;
	} else {
		return false;
	}
}

/** Recursive part of highlight_expr(). */
static bool highlight_expr_recursive(const struct bfs_ctx *ctx, const struct bfs_expr *expr, bool args[]) {
	if (!expr) {
		return false;
	}

	bool ret = false;

	for (size_t i = 0; i < ctx->argc; ++i) {
		if (&ctx->argv[i] == expr->argv) {
			for (size_t j = 0; j < expr->argc; ++j) {
				bfs_assert(i + j < ctx->argc);
				args[i + j] = true;
				ret = true;
			}
			break;
		}
	}

	for (struct bfs_expr *child = bfs_expr_children(expr); child; child = child->next) {
		ret |= highlight_expr_recursive(ctx, child, args);
	}

	return ret;
}

/** Highlight an expression in the command line. */
static bool highlight_expr(const struct bfs_ctx *ctx, const struct bfs_expr *expr, bool args[]) {
	for (size_t i = 0; i < ctx->argc; ++i) {
		args[i] = false;
	}

	return highlight_expr_recursive(ctx, expr, args);
}

/** Print a highlighted portion of the command line. */
static void bfs_argv_diag(const struct bfs_ctx *ctx, const bool args[], bool warning) {
	if (warning) {
		bfs_warning_prefix(ctx);
	} else {
		bfs_error_prefix(ctx);
	}

	dchar **argv = ZALLOC_ARRAY(dchar *, ctx->argc);
	if (!argv) {
		return;
	}

	for (size_t i = 0; i < ctx->argc; ++i) {
		if (dstrescat(&argv[i], ctx->argv[i], WESC_SHELL | WESC_TTY) != 0) {
			goto done;
		}
	}

	size_t max_argc = 0;
	for (size_t i = 0; i < ctx->argc; ++i) {
		if (i > 0) {
			cfprintf(ctx->cerr, " ");
		}

		if (args[i]) {
			max_argc = i + 1;
			cfprintf(ctx->cerr, "${bld}%s${rs}", argv[i]);
		} else {
			cfprintf(ctx->cerr, "%s", argv[i]);
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
				cfprintf(ctx->cerr, "${wrn}");
			} else {
				cfprintf(ctx->cerr, "${err}");
			}
		}

		size_t len = xstrwidth(argv[i]);
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

done:
	for (size_t i = 0; i < ctx->argc; ++i) {
		dstrfree(argv[i]);
	}
	free(argv);
}

void bfs_argv_error(const struct bfs_ctx *ctx, const bool args[]) {
	bfs_argv_diag(ctx, args, false);
}

void bfs_expr_error(const struct bfs_ctx *ctx, const struct bfs_expr *expr) {
	bool args[ctx->argc];
	if (highlight_expr(ctx, expr, args)) {
		bfs_argv_error(ctx, args);
	}
}

bool bfs_argv_warning(const struct bfs_ctx *ctx, const bool args[]) {
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
