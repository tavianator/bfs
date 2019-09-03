/****************************************************************************
 * bfs                                                                      *
 * Copyright (C) 2016-2019 Tavian Barnes <tavianator@tavianator.com>        *
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

#include "dstring.h"
#include <assert.h>
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
	char data[];
};

/** Get the string header from the string data pointer. */
static struct dstring *dstrheader(const char *dstr) {
	return (struct dstring *)(dstr - offsetof(struct dstring, data));
}

/** Get the correct size for a dstring with the given capacity. */
static size_t dstrsize(size_t capacity) {
	return sizeof(struct dstring) + capacity + 1;
}

/** Allocate a dstring with the given contents. */
static char *dstralloc_impl(size_t capacity, size_t length, const char *data) {
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

char *dstralloc(size_t capacity) {
	return dstralloc_impl(capacity, 0, "");
}

char *dstrdup(const char *str) {
	size_t len = strlen(str);
	return dstralloc_impl(len, len, str);
}

char *dstrndup(const char *str, size_t n) {
	size_t len = strnlen(str, n);
	return dstralloc_impl(len, len, str);
}

size_t dstrlen(const char *dstr) {
	return dstrheader(dstr)->length;
}

int dstreserve(char **dstr, size_t capacity) {
	struct dstring *header = dstrheader(*dstr);

	if (capacity > header->capacity) {
		capacity *= 2;

		header = realloc(header, dstrsize(capacity));
		if (!header) {
			return -1;
		}
		header->capacity = capacity;

		*dstr = header->data;
	}

	return 0;
}

int dstresize(char **dstr, size_t length) {
	if (dstreserve(dstr, length) != 0) {
		return -1;
	}

	struct dstring *header = dstrheader(*dstr);
	header->length = length;
	header->data[length] = '\0';

	return 0;
}

/** Common implementation of dstr{cat,ncat,app}. */
static int dstrcat_impl(char **dest, const char *src, size_t srclen) {
	size_t oldlen = dstrlen(*dest);
	size_t newlen = oldlen + srclen;

	if (dstresize(dest, newlen) != 0) {
		return -1;
	}

	memcpy(*dest + oldlen, src, srclen);
	return 0;
}

int dstrcat(char **dest, const char *src) {
	return dstrcat_impl(dest, src, strlen(src));
}

int dstrncat(char **dest, const char *src, size_t n) {
	return dstrcat_impl(dest, src, strnlen(src, n));
}

int dstrapp(char **str, char c) {
	return dstrcat_impl(str, &c, 1);
}

char *dstrprintf(const char *format, ...) {
	va_list args;

	va_start(args, format);
	int len = vsnprintf(NULL, 0, format, args);
	va_end(args);

	assert(len > 0);

	char *str = dstralloc(len);
	if (!str) {
		return NULL;
	}

	va_start(args, format);
	len = vsnprintf(str, len + 1, format, args);
	va_end(args);

	struct dstring *header = dstrheader(str);
	assert(len == header->capacity);
	header->length = len;

	return str;
}

void dstrfree(char *dstr) {
	if (dstr) {
		free(dstrheader(dstr));
	}
}
