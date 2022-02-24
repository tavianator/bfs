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
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#if BFS_WITH_ONIGURUMA
#	include <oniguruma.h>
#else
#	include <regex.h>
#endif

struct bfs_regex {
#if BFS_WITH_ONIGURUMA
	OnigRegex impl;
#else
	regex_t impl;
#endif
};

struct bfs_regex *bfs_regcomp(const char *expr, enum bfs_regex_type type, enum bfs_regcomp_flags flags, int *err) {
#if BFS_WITH_ONIGURUMA
	static bool onig_initialized = false;
	if (!onig_initialized) {
		OnigEncoding encs[] = {ONIG_ENCODING_UTF8};
		*err = onig_initialize(encs, sizeof(encs)/sizeof(encs[0]));
		if (*err != ONIG_NORMAL) {
			return NULL;
		}
		onig_initialized = true;
	}
#endif

	struct bfs_regex *regex = malloc(sizeof(*regex));
	if (!regex) {
#if BFS_WITH_ONIGURUMA
		*err = ONIGERR_MEMORY;
#else
		*err = REG_ESPACE;
#endif
		return NULL;
	}

#if BFS_WITH_ONIGURUMA
	OnigSyntaxType *syntax = NULL;
	switch (type) {
	case BFS_REGEX_POSIX_BASIC:
		syntax = ONIG_SYNTAX_POSIX_BASIC;
		break;
	case BFS_REGEX_POSIX_EXTENDED:
		syntax = ONIG_SYNTAX_POSIX_EXTENDED;
		break;
	case BFS_REGEX_EMACS:
		syntax = ONIG_SYNTAX_EMACS;
		break;
	case BFS_REGEX_GREP:
		syntax = ONIG_SYNTAX_GREP;
		break;
	}
	assert(syntax);

	OnigOptionType options = syntax->options;
	if (flags & BFS_REGEX_ICASE) {
		options |= ONIG_OPTION_IGNORECASE;
	}

	const unsigned char *uexpr = (const unsigned char *)expr;
	const unsigned char *end = uexpr + strlen(expr);
	*err = onig_new(&regex->impl, uexpr, end, options, ONIG_ENCODING_UTF8, syntax, NULL);
	if (*err != ONIG_NORMAL) {
		goto fail;
	}
#else
	int cflags = 0;
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

	if (flags & BFS_REGEX_ICASE) {
		cflags |= REG_ICASE;
	}

	*err = regcomp(&regex->impl, expr, cflags);
	if (*err != 0) {
		goto fail;
	}
#endif

	return regex;

fail:
	free(regex);
	return NULL;
}

bool bfs_regexec(struct bfs_regex *regex, const char *str, enum bfs_regexec_flags flags, int *err) {
	size_t len = strlen(str);

#if BFS_WITH_ONIGURUMA
	const unsigned char *ustr = (const unsigned char *)str;
	const unsigned char *end = ustr + len;

	// The docs for onig_{match,search}() say
	//
	//     Do not pass invalid byte string in the regex character encoding.
	if (!onigenc_is_valid_mbc_string(onig_get_encoding(regex->impl), ustr, end)) {
		*err = 0;
		return false;
	}

	int ret;
	if (flags & BFS_REGEX_ANCHOR) {
		ret = onig_match(regex->impl, ustr, end, ustr, NULL, ONIG_OPTION_DEFAULT);
	} else {
		ret = onig_search(regex->impl, ustr, end, ustr, end, NULL, ONIG_OPTION_DEFAULT);
	}

	if (ret >= 0) {
		*err = 0;
		if (flags & BFS_REGEX_ANCHOR) {
			return (size_t)ret == len;
		} else {
			return true;
		}
	} else if (ret == ONIG_MISMATCH) {
		*err = 0;
	} else {
		*err = ret;
	}

	return false;
#else
	regmatch_t match = {
		.rm_so = 0,
		.rm_eo = len,
	};

	int eflags = 0;
#ifdef REG_STARTEND
	eflags |= REG_STARTEND;
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
	} else {
		*err = ret;
	}

	return false;
#endif
}

void bfs_regfree(struct bfs_regex *regex) {
	if (regex) {
#if BFS_WITH_ONIGURUMA
		onig_free(regex->impl);
#else
		regfree(&regex->impl);
#endif
		free(regex);
	}
}

char *bfs_regerror(int err, const struct bfs_regex *regex) {
#if BFS_WITH_ONIGURUMA
	unsigned char *str = malloc(ONIG_MAX_ERROR_MESSAGE_LEN);
	if (str) {
		onig_error_code_to_str(str, err);
	}
	return (char *)str;
#else
	const regex_t *impl = regex ? &regex->impl : NULL;

	size_t len = regerror(err, impl, NULL, 0);
	char *str = malloc(len);
	if (str) {
		regerror(err, impl, str, len);
	}
	return str;
#endif
}
