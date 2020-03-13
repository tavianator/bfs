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
#include "cmdline.h"
#include "color.h"
#include "util.h"
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

void bfs_error(const struct cmdline *cmdline, const char *format, ...)  {
	va_list args;
	va_start(args, format);
	bfs_verror(cmdline, format, args);
	va_end(args);
}

void bfs_warning(const struct cmdline *cmdline, const char *format, ...)  {
	va_list args;
	va_start(args, format);
	bfs_vwarning(cmdline, format, args);
	va_end(args);
}

void bfs_verror(const struct cmdline *cmdline, const char *format, va_list args) {
	int error = errno;

	bfs_error_prefix(cmdline);

	errno = error;
	cvfprintf(cmdline->cerr, format, args);
}

void bfs_vwarning(const struct cmdline *cmdline, const char *format, va_list args) {
	if (cmdline->warn) {
		int error = errno;

		bfs_warning_prefix(cmdline);

		errno = error;
		cvfprintf(cmdline->cerr, format, args);
	}
}

void bfs_error_prefix(const struct cmdline *cmdline) {
	cfprintf(cmdline->cerr, "${bld}%s:${rs} ${er}error:${rs} ", xbasename(cmdline->argv[0]));
}

void bfs_warning_prefix(const struct cmdline *cmdline) {
	cfprintf(cmdline->cerr, "${bld}%s:${rs} ${wr}warning:${rs} ", xbasename(cmdline->argv[0]));
}
