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

#include "regex.h"
#include <stdlib.h>

int bfs_regcomp(regex_t *preg, const char *regex, int cflags, enum bfs_regex_type type) {
#if BFS_WITH_ONIGURUMA
	// Oniguruma's POSIX wrapper uses the selected default syntax when REG_EXTENDED is set
	cflags |= REG_EXTENDED;

	switch (type) {
	case BFS_REGEX_POSIX_BASIC:
		onig_set_default_syntax(ONIG_SYNTAX_POSIX_BASIC);
		break;
	case BFS_REGEX_POSIX_EXTENDED:
		onig_set_default_syntax(ONIG_SYNTAX_POSIX_EXTENDED);
		break;
	case BFS_REGEX_EMACS:
		onig_set_default_syntax(ONIG_SYNTAX_EMACS);
		break;
	case BFS_REGEX_GREP:
		onig_set_default_syntax(ONIG_SYNTAX_GREP);
		break;
	}
#else
	switch (type) {
	case BFS_REGEX_POSIX_BASIC:
		cflags &= ~REG_EXTENDED;
		break;
	case BFS_REGEX_POSIX_EXTENDED:
		cflags |= REG_EXTENDED;
		break;
	default:
		return REG_BADPAT;
	}
#endif

	return regcomp(preg, regex, cflags);
}

char *bfs_regerror(int err, const regex_t *regex) {
	size_t len = regerror(err, regex, NULL, 0);
	char *str = malloc(len);
	if (str) {
		regerror(err, regex, str, len);
	}
	return str;
}
