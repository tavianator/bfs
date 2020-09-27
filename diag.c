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

#include "diag.h"
#include "ctx.h"
#include "color.h"
#include "util.h"
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

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
	if (!(ctx->debug & flag)) {
		return false;
	}

	const char *str;

	switch (flag) {
	case DEBUG_COST:
		str = "cost";
		break;
	case DEBUG_EXEC:
		str = "exec";
		break;
	case DEBUG_OPT:
		str = "opt";
		break;
	case DEBUG_RATES:
		str = "rates";
		break;
	case DEBUG_SEARCH:
		str = "search";
		break;
	case DEBUG_STAT:
		str = "stat";
		break;
	case DEBUG_TREE:
		str = "tree";
		break;
	default:
		assert(false);
		str = "???";
		break;
	}

	cfprintf(ctx->cerr, "${bld}%s:${rs} ${cyn}-D %s${rs}: ", xbasename(ctx->argv[0]), str);
	return true;
}
