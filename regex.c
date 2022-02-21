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
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#if BFS_WITH_ONIGURUMA
#	include <onigposix.h>
#else
#	include <regex.h>
#endif

struct bfs_regex {
	regex_t impl;
};

struct bfs_regex *bfs_regcomp(const char *expr, enum bfs_regex_type type, enum bfs_regcomp_flags flags, int *err) {
	struct bfs_regex *regex = malloc(sizeof(*regex));
	if (!regex) {
		*err = REG_ESPACE;
		return NULL;
	}

	int cflags = 0;

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
		break;
	case BFS_REGEX_POSIX_EXTENDED:
		cflags |= REG_EXTENDED;
		break;
	default:
		*err = REG_BADPAT;
		goto fail;
	}
#endif

	if (flags & BFS_REGEX_ICASE) {
		cflags |= REG_ICASE;
	}

	*err = regcomp(&regex->impl, expr, cflags);
	if (*err != 0) {
		goto fail;
	}

	return regex;

fail:
	free(regex);
	return NULL;
}

bool bfs_regexec(struct bfs_regex *regex, const char *str, enum bfs_regexec_flags flags, int *err) {
	size_t len = strlen(str);
	regmatch_t match = {
		.rm_so = 0,
		.rm_eo = len,
	};

	int eflags = 0;
#ifdef REG_STARTEND
	if (flags & BFS_REGEX_ANCHOR) {
		eflags |= REG_STARTEND;
	}
#endif

	int ret = regexec(&regex->impl, str, 1, &match, eflags);
	if (ret == 0) {
		*err = 0;
		if (flags & BFS_REGEX_ANCHOR) {
			return match.rm_so == 0 && (size_t)match.rm_eo == len;
		} else {
			return true;
		}
	} else if (ret == REG_NOMATCH) {
		*err = 0;
		return false;
	} else {
		*err = ret;
		return false;
	}
}

void bfs_regfree(struct bfs_regex *regex) {
	if (regex) {
		regfree(&regex->impl);
		free(regex);
	}
}

char *bfs_regerror(int err, const struct bfs_regex *regex) {
	const regex_t *impl = regex ? &regex->impl : NULL;

	size_t len = regerror(err, impl, NULL, 0);
	char *str = malloc(len);
	if (str) {
		regerror(err, impl, str, len);
	}
	return str;
}
