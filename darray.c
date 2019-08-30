/****************************************************************************
 * bfs                                                                      *
 * Copyright (C) 2019 Tavian Barnes <tavianator@tavianator.com>             *
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

#include "darray.h"
#include <stdlib.h>
#include <string.h>

/**
 * The darray header.
 */
struct darray {
	size_t capacity;
	size_t length;
};

/** Get the header for a darray. */
static struct darray *darray_header(const void *da) {
	return (struct darray *)da - 1;
}

/** Get the array from a darray header. */
static char *darray_data(struct darray *header) {
	return (char *)(header + 1);
}

size_t darray_length(const void *da) {
	if (da) {
		return darray_header(da)->length;
	} else {
		return 0;
	}
}

void *darray_push(void *da, const void *item, size_t size) {
	struct darray *header;
	if (da) {
		header = darray_header(da);
	} else {
		header = malloc(sizeof(*header) + size);
		if (!header) {
			return NULL;
		}
		header->capacity = 1;
		header->length = 0;
	}

	size_t capacity = header->capacity;
	size_t i = header->length++;
	if (i >= capacity) {
		capacity *= 2;
		header = realloc(header, sizeof(*header) + capacity*size);
		if (!header) {
			// This failure will be detected by darray_check()
			return da;
		}
		header->capacity = capacity;
	}

	char *data = darray_data(header);
	memcpy(data + i*size, item, size);
	return data;
}

int darray_check(void *da) {
	if (!da) {
		return -1;
	}

	struct darray *header = darray_header(da);
	if (header->length <= header->capacity) {
		return 0;
	} else {
		header->length = header->capacity;
		return -1;
	}
}

void darray_free(void *da) {
	if (da) {
		free(darray_header(da));
	}
}
