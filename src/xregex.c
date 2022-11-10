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

#include "xregex.h"
#include "config.h"
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#if BFS_WITH_ONIGURUMA
#	include <langinfo.h>
#	include <oniguruma.h>
#else
#	include <regex.h>
#endif

struct bfs_regex {
#if BFS_WITH_ONIGURUMA
	unsigned char *pattern;
	OnigRegex impl;
	int err;
	OnigErrorInfo einfo;
#else
	regex_t impl;
	int err;
#endif
};

#if BFS_WITH_ONIGURUMA
/** Get (and initialize) the appropriate encoding for the current locale. */
static int bfs_onig_encoding(OnigEncoding *penc) {
	static OnigEncoding enc = NULL;
	if (enc) {
		*penc = enc;
		return ONIG_NORMAL;
	}

	// Fall back to ASCII by default
	enc = ONIG_ENCODING_ASCII;

	// Oniguruma has no locale support, so try to guess the right encoding
	// from the current locale.
	const char *charmap = nl_langinfo(CODESET);
	if (charmap) {
#define BFS_MAP_ENCODING(name, value)				\
		do {						\
			if (strcmp(charmap, name) == 0) {	\
				enc = value;			\
			}					\
		} while (0)
#define BFS_MAP_ENCODING2(name1, name2, value)		\
		do {					\
			BFS_MAP_ENCODING(name1, value);	\
			BFS_MAP_ENCODING(name2, value);	\
		} while (0)

		// These names were found with locale -m on Linux and FreeBSD
#define BFS_MAP_ISO_8859(n)						\
		BFS_MAP_ENCODING2("ISO-8859-" #n, "ISO8859-" #n, ONIG_ENCODING_ISO_8859_ ## n)

		BFS_MAP_ISO_8859(1);
		BFS_MAP_ISO_8859(2);
		BFS_MAP_ISO_8859(3);
		BFS_MAP_ISO_8859(4);
		BFS_MAP_ISO_8859(5);
		BFS_MAP_ISO_8859(6);
		BFS_MAP_ISO_8859(7);
		BFS_MAP_ISO_8859(8);
		BFS_MAP_ISO_8859(9);
		BFS_MAP_ISO_8859(10);
		BFS_MAP_ISO_8859(11);
		// BFS_MAP_ISO_8859(12);
		BFS_MAP_ISO_8859(13);
		BFS_MAP_ISO_8859(14);
		BFS_MAP_ISO_8859(15);
		BFS_MAP_ISO_8859(16);

		BFS_MAP_ENCODING("UTF-8", ONIG_ENCODING_UTF8);

#define BFS_MAP_EUC(name)						\
		BFS_MAP_ENCODING2("EUC-" #name, "euc" #name, ONIG_ENCODING_EUC_ ## name)

		BFS_MAP_EUC(JP);
		BFS_MAP_EUC(TW);
		BFS_MAP_EUC(KR);
		BFS_MAP_EUC(CN);

		BFS_MAP_ENCODING2("SHIFT_JIS", "SJIS", ONIG_ENCODING_SJIS);

		// BFS_MAP_ENCODING("KOI-8", ONIG_ENCODING_KOI8);
		BFS_MAP_ENCODING("KOI8-R", ONIG_ENCODING_KOI8_R);

		BFS_MAP_ENCODING("CP1251", ONIG_ENCODING_CP1251);

		BFS_MAP_ENCODING("GB18030", ONIG_ENCODING_BIG5);
	}

	int ret = onig_initialize(&enc, 1);
	if (ret != ONIG_NORMAL) {
		enc = NULL;
	}
	*penc = enc;
	return ret;
}
#endif

int bfs_regcomp(struct bfs_regex **preg, const char *pattern, enum bfs_regex_type type, enum bfs_regcomp_flags flags) {
	struct bfs_regex *regex = *preg = malloc(sizeof(*regex));
	if (!regex) {
		return -1;
	}

#if BFS_WITH_ONIGURUMA
	// onig_error_code_to_str() says
	//
	//     don't call this after the pattern argument of onig_new() is freed
	//
	// so make a defensive copy.
	regex->pattern = (unsigned char *)strdup(pattern);
	if (!regex->pattern) {
		goto fail;
	}

	regex->impl = NULL;
	regex->err = ONIG_NORMAL;

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

	OnigEncoding enc;
	regex->err = bfs_onig_encoding(&enc);
	if (regex->err != ONIG_NORMAL) {
		return -1;
	}

	const unsigned char *end = regex->pattern + strlen(pattern);
	regex->err = onig_new(&regex->impl, regex->pattern, end, options, enc, syntax, &regex->einfo);
	if (regex->err != ONIG_NORMAL) {
		return -1;
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
		errno = EINVAL;
		goto fail;
	}

	if (flags & BFS_REGEX_ICASE) {
		cflags |= REG_ICASE;
	}

#if __has_feature(memory_sanitizer)
	// https://github.com/google/sanitizers/issues/1496
	memset(&regex->impl, 0, sizeof(regex->impl));
#endif

	regex->err = regcomp(&regex->impl, pattern, cflags);
	if (regex->err != 0) {
		return -1;
	}
#endif

	return 0;

fail:
	free(regex);
	*preg = NULL;
	return -1;
}

int bfs_regexec(struct bfs_regex *regex, const char *str, enum bfs_regexec_flags flags) {
	size_t len = strlen(str);

#if BFS_WITH_ONIGURUMA
	const unsigned char *ustr = (const unsigned char *)str;
	const unsigned char *end = ustr + len;

	// The docs for onig_{match,search}() say
	//
	//     Do not pass invalid byte string in the regex character encoding.
	if (!onigenc_is_valid_mbc_string(onig_get_encoding(regex->impl), ustr, end)) {
		return 0;
	}

	int ret;
	if (flags & BFS_REGEX_ANCHOR) {
		ret = onig_match(regex->impl, ustr, end, ustr, NULL, ONIG_OPTION_DEFAULT);
	} else {
		ret = onig_search(regex->impl, ustr, end, ustr, end, NULL, ONIG_OPTION_DEFAULT);
	}

	if (ret >= 0) {
		if (flags & BFS_REGEX_ANCHOR) {
			return (size_t)ret == len;
		} else {
			return 1;
		}
	} else if (ret == ONIG_MISMATCH) {
		return 0;
	} else {
		regex->err = ret;
		return -1;
	}
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
		if (flags & BFS_REGEX_ANCHOR) {
			return match.rm_so == 0 && (size_t)match.rm_eo == len;
		} else {
			return 1;
		}
	} else if (ret == REG_NOMATCH) {
		return 0;
	} else {
		regex->err = ret;
		return -1;
	}
#endif
}

void bfs_regfree(struct bfs_regex *regex) {
	if (regex) {
#if BFS_WITH_ONIGURUMA
		onig_free(regex->impl);
		free(regex->pattern);
#else
		regfree(&regex->impl);
#endif
		free(regex);
	}
}

char *bfs_regerror(const struct bfs_regex *regex) {
	if (!regex) {
		return strdup(strerror(ENOMEM));
	}

#if BFS_WITH_ONIGURUMA
	unsigned char *str = malloc(ONIG_MAX_ERROR_MESSAGE_LEN);
	if (str) {
		onig_error_code_to_str(str, regex->err, &regex->einfo);
	}
	return (char *)str;
#else
	size_t len = regerror(regex->err, &regex->impl, NULL, 0);
	char *str = malloc(len);
	if (str) {
		regerror(regex->err, &regex->impl, str, len);
	}
	return str;
#endif
}
