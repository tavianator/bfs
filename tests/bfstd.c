// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include "prelude.h"
#include "tests.h"
#include "bfstd.h"
#include "diag.h"
#include <errno.h>
#include <langinfo.h>
#include <stdlib.h>
#include <string.h>

/** Check the result of xdirname()/xbasename(). */
static void check_base_dir(const char *path, const char *dir, const char *base) {
	char *xdir = xdirname(path);
	bfs_everify(xdir, "xdirname()");
	bfs_check(strcmp(xdir, dir) == 0, "xdirname('%s') == '%s' (!= '%s')", path, xdir, dir);
	free(xdir);

	char *xbase = xbasename(path);
	bfs_everify(xbase, "xbasename()");
	bfs_check(strcmp(xbase, base) == 0, "xbasename('%s') == '%s' (!= '%s')", path, xbase, base);
	free(xbase);
}

/** Check the result of wordesc(). */
static void check_wordesc(const char *str, const char *exp, enum wesc_flags flags) {
	char buf[256];
	char *end = buf + sizeof(buf);
	char *esc = wordesc(buf, end, str, flags);

	if (bfs_check(esc != end)) {
		bfs_check(strcmp(buf, exp) == 0, "wordesc('%s') == '%s' (!= '%s')", str, buf, exp);
	}
}

void check_bfstd(void) {
	bfs_check(asciilen("") == 0);
	bfs_check(asciilen("@") == 1);
	bfs_check(asciilen("@@") == 2);
	bfs_check(asciilen("\xFF@") == 0);
	bfs_check(asciilen("@\xFF") == 1);
	bfs_check(asciilen("@@@@@@@@") == 8);
	bfs_check(asciilen("@@@@@@@@@@@@@@@@") == 16);
	bfs_check(asciilen("@@@@@@@@@@@@@@@@@@@@@@@@") == 24);
	bfs_check(asciilen("@@@@@@@@@@@@@@a\xFF@@@@@@@") == 15);
	bfs_check(asciilen("@@@@@@@@@@@@@@@@\xFF@@@@@@@") == 16);
	bfs_check(asciilen("@@@@@@@@@@@@@@@@a\xFF@@@@@@") == 17);
	bfs_check(asciilen("@@@@@@@\xFF@@@@@@a\xFF@@@@@@@") == 7);
	bfs_check(asciilen("@@@@@@@@\xFF@@@@@a\xFF@@@@@@@") == 8);
	bfs_check(asciilen("@@@@@@@@@\xFF@@@@a\xFF@@@@@@@") == 9);

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
	check_wordesc("~user", "\"~user\"", WESC_SHELL);

	const char *charmap = nl_langinfo(CODESET);
	if (strcmp(charmap, "UTF-8") == 0) {
		check_wordesc("\xF0", "$'\\xF0'", WESC_SHELL | WESC_TTY);
		check_wordesc("\xF0\x9F", "$'\\xF0\\x9F'", WESC_SHELL | WESC_TTY);
		check_wordesc("\xF0\x9F\x98", "$'\\xF0\\x9F\\x98'", WESC_SHELL | WESC_TTY);
		check_wordesc("\xF0\x9F\x98\x80", "\xF0\x9F\x98\x80", WESC_SHELL | WESC_TTY);
		check_wordesc("\xCB\x9Cuser", "\xCB\x9Cuser", WESC_SHELL);
	}

	bfs_check(xstrwidth("Hello world") == 11);
	bfs_check(xstrwidth("Hello\1world") == 10);
}
