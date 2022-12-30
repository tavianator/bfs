/****************************************************************************
 * bfs                                                                      *
 * Copyright (C) 2022 Tavian Barnes <tavianator@tavianator.com>             *
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

#include "../src/bfstd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Check the result of xdirname()/xbasename(). */
static void check_base_dir(const char *path, const char *dir, const char *base) {
	char *xdir = xdirname(path);
	if (!xdir) {
		perror("xdirname()");
		abort();
	} else if (strcmp(xdir, dir) != 0) {
		fprintf(stderr, "xdirname(\"%s\") == \"%s\" (!= \"%s\")\n", path, xdir, dir);
		abort();
	}
	free(xdir);

	char *xbase = xbasename(path);
	if (!xbase) {
		perror("xbasename()");
		abort();
	} else if (strcmp(xbase, base) != 0) {
		fprintf(stderr, "xbasename(\"%s\") == \"%s\" (!= \"%s\")\n", path, xbase, base);
		abort();
	}
	free(xbase);
}

int main(void) {
	// From man 3p basename
	check_base_dir("usr", ".", "usr");
	check_base_dir("usr/", ".", "usr");
	check_base_dir("", ".", ".");
	check_base_dir("/", "/", "/");
	// check_base_dir("//", "/" or "//", "/" or "//");
	check_base_dir("///", "/", "/");
	check_base_dir("/usr/", "/", "usr");
	check_base_dir("/usr/lib", "/usr", "lib");
	check_base_dir("//usr//lib//", "//usr", "lib");
	check_base_dir("/home//dwc//test", "/home//dwc", "test");
}
