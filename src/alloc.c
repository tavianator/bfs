// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include "alloc.h"
#include "bit.h"
#include "diag.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

/** Portable aligned_alloc()/posix_memalign(). */
static void *xmemalign(size_t align, size_t size) {
	bfs_assert(has_single_bit(align));
	bfs_assert(align >= sizeof(void *));
	bfs_assert((size & (align - 1)) == 0);

#if __APPLE__
	void *ptr = NULL;
	errno = posix_memalign(&ptr, align, size);
	return ptr;
#else
	return aligned_alloc(align, size);
#endif
}

void *alloc(size_t align, size_t size) {
	bfs_assert(has_single_bit(align));
	bfs_assert((size & (align - 1)) == 0);

	if (align <= alignof(max_align_t)) {
		return malloc(size);
	} else {
		return xmemalign(align, size);
	}
}

void *zalloc(size_t align, size_t size) {
	bfs_assert(has_single_bit(align));
	bfs_assert((size & (align - 1)) == 0);

	if (align <= alignof(max_align_t)) {
		return calloc(1, size);
	}

	void *ret = xmemalign(align, size);
	if (ret) {
		memset(ret, 0, size);
	}
	return ret;
}
