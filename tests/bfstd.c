// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include "tests.h"
#include "../src/bfstd.h"
#include "../src/config.h"
#include "../src/diag.h"
#include <errno.h>
#include <langinfo.h>
#include <locale.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Check the result of xdirname()/xbasename(). */
static bool check_base_dir(const char *path, const char *dir, const char *base) {
	bool ret = true;

	char *xdir = xdirname(path);
	bfs_verify(xdir, "xdirname(): %s", xstrerror(errno));
	ret &= bfs_check(strcmp(xdir, dir) == 0, "xdirname('%s') == '%s' (!= '%s')", path, xdir, dir);
	free(xdir);

	char *xbase = xbasename(path);
	bfs_verify(xbase, "xbasename(): %s", xstrerror(errno));
	ret &= bfs_check(strcmp(xbase, base) == 0, "xbasename('%s') == '%s' (!= '%s')", path, xbase, base);
	free(xbase);

	return ret;
}

/** Check the result of wordesc(). */
static bool check_wordesc(const char *str, const char *exp, enum wesc_flags flags) {
	char buf[256];
	char *end = buf + sizeof(buf);
	char *esc = wordesc(buf, end, str, flags);

	return bfs_check(esc != end)
		&& bfs_check(strcmp(buf, exp) == 0, "wordesc('%s') == '%s' (!= '%s')", str, buf, exp);
}

bool check_bfstd(void) {
	bool ret = true;

	// Try to set a UTF-8 locale
	if (!setlocale(LC_ALL, "C.UTF-8")) {
		setlocale(LC_ALL, "");
	}

	// From man 3p basename
	ret &= check_base_dir("usr", ".", "usr");
	ret &= check_base_dir("usr/", ".", "usr");
	ret &= check_base_dir("", ".", ".");
	ret &= check_base_dir("/", "/", "/");
	// check_base_dir("//", "/" or "//", "/" or "//");
	ret &= check_base_dir("///", "/", "/");
	ret &= check_base_dir("/usr/", "/", "usr");
	ret &= check_base_dir("/usr/lib", "/usr", "lib");
	ret &= check_base_dir("//usr//lib//", "//usr", "lib");
	ret &= check_base_dir("/home//dwc//test", "/home//dwc", "test");

	ret &= check_wordesc("", "\"\"", WESC_SHELL);
	ret &= check_wordesc("word", "word", WESC_SHELL);
	ret &= check_wordesc("two words", "\"two words\"", WESC_SHELL);
	ret &= check_wordesc("word's", "\"word's\"", WESC_SHELL);
	ret &= check_wordesc("\"word\"", "'\"word\"'", WESC_SHELL);
	ret &= check_wordesc("\"word's\"", "'\"word'\\''s\"'", WESC_SHELL);
	ret &= check_wordesc("\033[1mbold's\033[0m", "$'\\e[1mbold\\'s\\e[0m'", WESC_SHELL | WESC_TTY);
	ret &= check_wordesc("\x7F", "$'\\x7F'", WESC_SHELL | WESC_TTY);

	const char *charmap = nl_langinfo(CODESET);
	if (strcmp(charmap, "UTF-8") == 0) {
		ret &= check_wordesc("\xF0", "$'\\xF0'", WESC_SHELL | WESC_TTY);
		ret &= check_wordesc("\xF0\x9F", "$'\\xF0\\x9F'", WESC_SHELL | WESC_TTY);
		ret &= check_wordesc("\xF0\x9F\x98", "$'\\xF0\\x9F\\x98'", WESC_SHELL | WESC_TTY);
		ret &= check_wordesc("\xF0\x9F\x98\x80", "\xF0\x9F\x98\x80", WESC_SHELL | WESC_TTY);
	}

	return ret;
}
