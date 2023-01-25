// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

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
