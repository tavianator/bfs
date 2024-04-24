// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include "prelude.h"
#include "dstring.h"
#include "alloc.h"
#include "bit.h"
#include "diag.h"
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * The memory representation of a dynamic string.  Users get a pointer to str.
 */
struct dstring {
	/** Capacity of the string, *including* the terminating NUL. */
	size_t cap;
	/** Length of the string, *excluding* the terminating NUL. */
	size_t len;
	/** The string itself. */
	alignas(dchar) char str[];
};

#define DSTR_OFFSET offsetof(struct dstring, str)

/** Back up to the header from a pointer to dstring::str. */
static struct dstring *dstrheader(const dchar *dstr) {
	return (struct dstring *)(dstr - DSTR_OFFSET);
}

/**
 * In some provenance models, the expression `header->str` has its provenance
 * restricted to just the `str` field itself, making a future dstrheader()
 * illegal.  This alternative is guaranteed to preserve provenance for the entire
 * allocation.
 *
 * - https://stackoverflow.com/q/25296019
 * - https://mastodon.social/@void_friend@tech.lgbt/111144859908104311
 */
static dchar *dstrdata(struct dstring *header) {
	return (char *)header + DSTR_OFFSET;
}

/** Allocate a dstring with the given contents. */
static dchar *dstralloc_impl(size_t cap, size_t len, const char *str) {
	// Avoid reallocations for small strings
	if (cap < DSTR_OFFSET) {
		cap = DSTR_OFFSET;
	}

	struct dstring *header = ALLOC_FLEX(struct dstring, str, cap);
	if (!header) {
		return NULL;
	}

	header->cap = cap;
	header->len = len;

	char *ret = dstrdata(header);
	memcpy(ret, str, len);
	ret[len] = '\0';
	return ret;
}

dchar *dstralloc(size_t cap) {
	return dstralloc_impl(cap + 1, 0, "");
}

dchar *dstrdup(const char *str) {
	return dstrxdup(str, strlen(str));
}

dchar *dstrndup(const char *str, size_t n) {
	return dstrxdup(str, strnlen(str, n));
}

dchar *dstrddup(const dchar *dstr) {
	return dstrxdup(dstr, dstrlen(dstr));
}

dchar *dstrxdup(const char *str, size_t len) {
	return dstralloc_impl(len + 1, len, str);
}

size_t dstrlen(const dchar *dstr) {
	return dstrheader(dstr)->len;
}

int dstreserve(dchar **dstr, size_t cap) {
	if (!*dstr) {
		*dstr = dstralloc(cap);
		return *dstr ? 0 : -1;
	}

	struct dstring *header = dstrheader(*dstr);
	size_t old_cap = header->cap;
	size_t new_cap = cap + 1; // Terminating NUL
	if (old_cap >= new_cap) {
		return 0;
	}

	new_cap = bit_ceil(new_cap);
	header = REALLOC_FLEX(struct dstring, str, header, old_cap, new_cap);
	if (!header) {
		return -1;
	}

	header->cap = new_cap;
	*dstr = dstrdata(header);
	return 0;
}

int dstresize(dchar **dstr, size_t len) {
	if (dstreserve(dstr, len) != 0) {
		return -1;
	}

	struct dstring *header = dstrheader(*dstr);
	header->len = len;
	header->str[len] = '\0';
	return 0;
}

int dstrcat(dchar **dest, const char *src) {
	return dstrxcat(dest, src, strlen(src));
}

int dstrncat(dchar **dest, const char *src, size_t n) {
	return dstrxcat(dest, src, strnlen(src, n));
}

int dstrdcat(dchar **dest, const dchar *src) {
	return dstrxcat(dest, src, dstrlen(src));
}

int dstrxcat(dchar **dest, const char *src, size_t len) {
	size_t oldlen = dstrlen(*dest);
	size_t newlen = oldlen + len;

	if (dstresize(dest, newlen) != 0) {
		return -1;
	}

	memcpy(*dest + oldlen, src, len);
	return 0;
}

int dstrapp(dchar **str, char c) {
	return dstrxcat(str, &c, 1);
}

int dstrcpy(dchar **dest, const char *src) {
	return dstrxcpy(dest, src, strlen(src));
}

int dstrncpy(dchar **dest, const char *src, size_t n) {
	return dstrxcpy(dest, src, strnlen(src, n));
}

int dstrdcpy(dchar **dest, const dchar *src) {
	return dstrxcpy(dest, src, dstrlen(src));
}

int dstrxcpy(dchar **dest, const char *src, size_t len) {
	if (dstresize(dest, len) != 0) {
		return -1;
	}

	memcpy(*dest, src, len);
	return 0;
}

char *dstrprintf(const char *format, ...) {
	va_list args;

	va_start(args, format);
	dchar *str = dstrvprintf(format, args);
	va_end(args);

	return str;
}

char *dstrvprintf(const char *format, va_list args) {
	// Guess a capacity to try to avoid reallocating
	dchar *str = dstralloc(2 * strlen(format));
	if (!str) {
		return NULL;
	}

	if (dstrvcatf(&str, format, args) != 0) {
		dstrfree(str);
		return NULL;
	}

	return str;
}

int dstrcatf(dchar **str, const char *format, ...) {
	va_list args;

	va_start(args, format);
	int ret = dstrvcatf(str, format, args);
	va_end(args);

	return ret;
}

int dstrvcatf(dchar **str, const char *format, va_list args) {
	// Guess a capacity to try to avoid calling vsnprintf() twice
	size_t len = dstrlen(*str);
	dstreserve(str, len + 2 * strlen(format));
	size_t cap = dstrheader(*str)->cap;

	va_list copy;
	va_copy(copy, args);

	char *tail = *str + len;
	size_t tail_cap = cap - len;
	int ret = vsnprintf(tail, tail_cap, format, args);
	if (ret < 0) {
		goto fail;
	}

	size_t tail_len = ret;
	if (tail_len >= tail_cap) {
		if (dstreserve(str, len + tail_len) != 0) {
			goto fail;
		}

		tail = *str + len;
		ret = vsnprintf(tail, tail_len + 1, format, copy);
		if (ret < 0 || (size_t)ret != tail_len) {
			bfs_bug("Length of formatted string changed");
			goto fail;
		}
	}

	va_end(copy);

	dstrheader(*str)->len += tail_len;
	return 0;

fail:
	va_end(copy);
	*tail = '\0';
	return -1;
}

int dstrescat(dchar **dest, const char *str, enum wesc_flags flags) {
	return dstrnescat(dest, str, SIZE_MAX, flags);
}

int dstrnescat(dchar **dest, const char *str, size_t n, enum wesc_flags flags) {
	size_t len = *dest ? dstrlen(*dest) : 0;

	// Worst case growth is `ccc...` => $'\xCC\xCC\xCC...'
	n = strnlen(str, n);
	size_t cap = len + 4 * n + 3;
	if (dstreserve(dest, cap) != 0) {
		return -1;
	}

	char *cur = *dest + len;
	char *end = *dest + cap + 1;
	cur = wordnesc(cur, end, str, n, flags);
	bfs_assert(cur != end, "wordesc() result truncated");

	return dstresize(dest, cur - *dest);
}

void dstrfree(dchar *dstr) {
	if (dstr) {
		free(dstrheader(dstr));
	}
}
