// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include "dstring.h"
#include "alloc.h"
#include "bit.h"
#include "config.h"
#include "diag.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * The memory representation of a dynamic string.  Users get a pointer to data.
 */
struct dstring {
	size_t capacity;
	size_t length;
	alignas(dchar) char data[];
};

/** Get the string header from the string data pointer. */
static struct dstring *dstrheader(const dchar *dstr) {
	return (struct dstring *)(dstr - offsetof(struct dstring, data));
}

/** Get the correct size for a dstring with the given capacity. */
static size_t dstrsize(size_t capacity) {
	return sizeof_flex(struct dstring, data, capacity + 1);
}

/** Allocate a dstring with the given contents. */
static dchar *dstralloc_impl(size_t capacity, size_t length, const char *data) {
	// Avoid reallocations for small strings
	if (capacity < 7) {
		capacity = 7;
	}

	struct dstring *header = malloc(dstrsize(capacity));
	if (!header) {
		return NULL;
	}

	header->capacity = capacity;
	header->length = length;

	memcpy(header->data, data, length);
	header->data[length] = '\0';
	return header->data;
}

dchar *dstralloc(size_t capacity) {
	return dstralloc_impl(capacity, 0, "");
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
	return dstralloc_impl(len, len, str);
}

size_t dstrlen(const dchar *dstr) {
	return dstrheader(dstr)->length;
}

int dstreserve(dchar **dstr, size_t capacity) {
	if (!*dstr) {
		*dstr = dstralloc(capacity);
		return *dstr ? 0 : -1;
	}

	struct dstring *header = dstrheader(*dstr);

	if (capacity > header->capacity) {
		capacity = bit_ceil(capacity + 1) - 1;

		header = realloc(header, dstrsize(capacity));
		if (!header) {
			return -1;
		}
		header->capacity = capacity;

		*dstr = header->data;
	}

	return 0;
}

int dstresize(dchar **dstr, size_t length) {
	if (dstreserve(dstr, length) != 0) {
		return -1;
	}

	struct dstring *header = dstrheader(*dstr);
	header->length = length;
	header->data[length] = '\0';
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
	size_t cap = dstrheader(*str)->capacity;

	va_list copy;
	va_copy(copy, args);

	char *tail = *str + len;
	int ret = vsnprintf(tail, cap - len + 1, format, args);
	if (ret < 0) {
		goto fail;
	}

	size_t tail_len = ret;
	if (tail_len > cap - len) {
		cap = len + tail_len;
		if (dstreserve(str, cap) != 0) {
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

	struct dstring *header = dstrheader(*str);
	header->length += tail_len;
	return 0;

fail:
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
