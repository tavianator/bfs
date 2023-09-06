// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include "../src/bfstd.h"
#include "../src/config.h"
#include "../src/diag.h"
#include <errno.h>
#include <langinfo.h>
#include <locale.h>
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

/** Check the result of wordesc(). */
static void check_wordesc(const char *str, const char *exp, enum wesc_flags flags) {
	char buf[256];
	char *end = buf + sizeof(buf);
	char *ret = wordesc(buf, end, str, flags);
	bfs_verify(ret != end);
	bfs_verify(strcmp(buf, exp) == 0, "wordesc(%s) == %s (!= %s)", str, buf, exp);
}

int main(void) {
	// Try to set a UTF-8 locale
	if (!setlocale(LC_ALL, "C.UTF-8")) {
		setlocale(LC_ALL, "");
	}

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

	check_wordesc("", "\"\"", WESC_SHELL);
	check_wordesc("word", "word", WESC_SHELL);
	check_wordesc("two words", "\"two words\"", WESC_SHELL);
	check_wordesc("word's", "\"word's\"", WESC_SHELL);
	check_wordesc("\"word\"", "'\"word\"'", WESC_SHELL);
	check_wordesc("\"word's\"", "'\"word'\\''s\"'", WESC_SHELL);
	check_wordesc("\033[1mbold's\033[0m", "$'\\e[1mbold\\'s\\e[0m'", WESC_SHELL | WESC_TTY);
	check_wordesc("\x7F", "$'\\x7F'", WESC_SHELL | WESC_TTY);

	const char *charmap = nl_langinfo(CODESET);
	if (strcmp(charmap, "UTF-8") == 0) {
		check_wordesc("\xF0", "$'\\xF0'", WESC_SHELL | WESC_TTY);
		check_wordesc("\xF0\x9F", "$'\\xF0\\x9F'", WESC_SHELL | WESC_TTY);
		check_wordesc("\xF0\x9F\x98", "$'\\xF0\\x9F\\x98'", WESC_SHELL | WESC_TTY);
		check_wordesc("\xF0\x9F\x98\x80", "\xF0\x9F\x98\x80", WESC_SHELL | WESC_TTY);
	}

	return EXIT_SUCCESS;
}
