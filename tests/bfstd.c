// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include "../src/bfstd.h"
#include "../src/config.h"
#include "../src/diag.h"
#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/** Check the result of xdirname()/xbasename(). */
static void check_base_dir(const char *path, const char *dir, const char *base) {
	char *xdir = xdirname(path);
	bfs_verify(xdir, "xdirname(): %s", strerror(errno));
	bfs_verify(strcmp(xdir, dir) == 0, "xdirname('%s') == '%s' (!= '%s')", path, xdir, dir);
	free(xdir);

	char *xbase = xbasename(path);
	bfs_verify(xbase, "xbasename(): %s", strerror(errno));
	bfs_verify(strcmp(xbase, base) == 0, "xbasename('%s') == '%s' (!= '%s')", path, xbase, base);
	free(xbase);
}

int main(void) {
	// Check flex_sizeof()
	struct flexible {
		alignas(64) int foo;
		int bar[];
	};
	bfs_verify(flex_sizeof(struct flexible, bar, 0) >= sizeof(struct flexible));
	bfs_verify(flex_sizeof(struct flexible, bar, 16) % alignof(struct flexible) == 0);
	bfs_verify(flex_sizeof(struct flexible, bar, SIZE_MAX / sizeof(int) + 1)
	           == align_floor(alignof(struct flexible), SIZE_MAX));
	bfs_verify(flex_sizeof_impl(8, 16, 4, 4, 1) == 16);

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
