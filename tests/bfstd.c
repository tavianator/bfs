// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include "tests.h"

#include "bfstd.h"
#include "diag.h"

#include <errno.h>
#include <langinfo.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/** asciilen() test cases. */
static void check_asciilen(void) {
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
}

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

/** xdirname()/xbasename() test cases. */
static void check_basedirs(void) {
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

/** Check the result of wordesc(). */
static void check_wordesc(const char *str, const char *exp, enum wesc_flags flags) {
	char buf[256];
	char *end = buf + sizeof(buf);
	char *esc = wordesc(buf, end, str, flags);

	if (bfs_check(esc != end)) {
		bfs_check(strcmp(buf, exp) == 0, "wordesc('%s') == '%s' (!= '%s')", str, buf, exp);
	}
}

/** wordesc() test cases. */
static void check_wordescs(void) {
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
}

/** xstrto*() test cases. */
static void check_strtox(void) {
	short s;
	unsigned short us;
	int i;
	unsigned int ui;
	long l;
	unsigned long ul;
	long long ll;
	unsigned long long ull;
	char *end;

#define check_strtouerr(err, str, end, base) \
	do { \
		bfs_echeck(xstrtous(str, end, base, &us) != 0 && errno == err); \
		bfs_echeck(xstrtoui(str, end, base, &ui) != 0 && errno == err); \
		bfs_echeck(xstrtoul(str, end, base, &ul) != 0 && errno == err); \
		bfs_echeck(xstrtoull(str, end, base, &ull) != 0 && errno == err); \
	} while (0)

	check_strtouerr(ERANGE, "-1", NULL, 0);
	check_strtouerr(ERANGE, "-0x1", NULL, 0);

	check_strtouerr(EINVAL, "-", NULL, 0);
	check_strtouerr(EINVAL, "-q", NULL, 0);
	check_strtouerr(EINVAL, "-1q", NULL, 0);
	check_strtouerr(EINVAL, "-0x", NULL, 0);

#define check_strtoerr(err, str, end, base) \
	do { \
		bfs_echeck(xstrtos(str, end, base, &s) != 0 && errno == err); \
		bfs_echeck(xstrtoi(str, end, base, &i) != 0 && errno == err); \
		bfs_echeck(xstrtol(str, end, base, &l) != 0 && errno == err); \
		bfs_echeck(xstrtoll(str, end, base, &ll) != 0 && errno == err); \
		check_strtouerr(err, str, end, base); \
	} while (0)

	check_strtoerr(EINVAL, "", NULL, 0);
	check_strtoerr(EINVAL, "", &end, 0);
	check_strtoerr(EINVAL, " 1 ", &end, 0);
	check_strtoerr(EINVAL, " -1", NULL, 0);
	check_strtoerr(EINVAL, " 123", NULL, 0);
	check_strtoerr(EINVAL, "123 ", NULL, 0);
	check_strtoerr(EINVAL, "0789", NULL, 0);
	check_strtoerr(EINVAL, "789A", NULL, 0);
	check_strtoerr(EINVAL, "0x", NULL, 0);
	check_strtoerr(EINVAL, "0x789A", NULL, 10);
	check_strtoerr(EINVAL, "0x-1", NULL, 0);

#define check_strtotype(type, min, max, fmt, fn, str, base, v, n) \
	do { \
		if ((n) >= min && (n) <= max) { \
			bfs_echeck(fn(str, NULL, base, &v) == 0); \
			bfs_check(v == (type)(n), "%s('%s') == " fmt " (!= " fmt ")", #fn, str, v, (type)(n)); \
		} else { \
			bfs_echeck(fn(str, NULL, base, &v) != 0 && errno == ERANGE); \
		} \
	} while (0)

#define check_strtoint(str, base, n) \
	do { \
		check_strtotype(  signed short,      SHRT_MIN,   SHRT_MAX, "%d",   xstrtos,   str, base,  s,  n); \
		check_strtotype(  signed int,         INT_MIN,    INT_MAX, "%d",   xstrtoi,   str, base,  i,  n); \
		check_strtotype(  signed long,       LONG_MIN,   LONG_MAX, "%ld",  xstrtol,   str, base,  l,  n); \
		check_strtotype(  signed long long, LLONG_MIN,  LLONG_MAX, "%lld", xstrtoll,  str, base, ll,  n); \
		check_strtotype(unsigned short,             0,  USHRT_MAX, "%u",   xstrtous,  str, base, us,  n); \
		check_strtotype(unsigned int,               0,   UINT_MAX, "%u",   xstrtoui,  str, base, ui,  n); \
		check_strtotype(unsigned long,              0,  ULONG_MAX, "%lu",  xstrtoul,  str, base, ul,  n); \
		check_strtotype(unsigned long long,         0, ULLONG_MAX, "%llu", xstrtoull, str, base, ull, n); \
	} while (0)

	check_strtoint("123", 0, 123);
	check_strtoint("+123", 0, 123);
	check_strtoint("-123", 0, -123);

	check_strtoint("0123", 0, 0123);
	check_strtoint("0x789A", 0, 0x789A);

	check_strtoint("0123", 10, 123);
	check_strtoint("0789", 10, 789);

	check_strtoint("123", 16, 0x123);

	check_strtoint("0x7FFF", 0, 0x7FFF);
	check_strtoint("-0x8000", 0, -0x8000);

	check_strtoint("0x7FFFFFFF", 0, 0x7FFFFFFFL);
	check_strtoint("-0x80000000", 0, -0x7FFFFFFFL - 1);

	check_strtoint("0x7FFFFFFFFFFFFFFF", 0, 0x7FFFFFFFFFFFFFFFLL);
	check_strtoint("-0x8000000000000000", 0, -0x7FFFFFFFFFFFFFFFLL - 1);

#define check_strtoend(str, estr, base, n) \
	do { \
		bfs_echeck(xstrtoll(str, &end, base, &ll) == 0); \
		bfs_check(ll == (n), "xstrtoll('%s') == %lld (!= %lld)", str, ll, (long long)(n)); \
		bfs_check(strcmp(end, estr) == 0, "xstrtoll('%s'): end == '%s' (!= '%s')", str, end, estr); \
	} while (0)

	check_strtoend("123 ", " ", 0, 123);
	check_strtoend("0789", "89", 0, 07);
	check_strtoend("789A", "A", 0, 789);
	check_strtoend("0xDEFG", "G", 0, 0xDEF);
}

/** xstrwidth() test cases. */
static void check_strwidth(void) {
	bfs_check(xstrwidth("Hello world") == 11);
	bfs_check(xstrwidth("Hello\1world") == 10);
}

void check_bfstd(void) {
	check_asciilen();
	check_basedirs();
	check_wordescs();
	check_strtox();
	check_strwidth();
}
