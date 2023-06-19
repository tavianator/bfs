// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include "alloc.h"
#include "bit.h"
#include "diag.h"
#include "sanity.h"
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

/**
 * An arena allocator chunk.
 */
union chunk {
	/**
	 * Free chunks are stored in a singly linked list.  The pointer to the
	 * next chunk is represented by an offset from the chunk immediately
	 * after this one in memory, so that zalloc() correctly initializes a
	 * linked list of chunks (except for the last one).
	 */
	uintptr_t next;

	// char object[];
};

/** Decode the next chunk. */
static union chunk *chunk_next(const struct arena *arena, const union chunk *chunk) {
	uintptr_t base = (uintptr_t)chunk + arena->size;
	return (union chunk *)(base + chunk->next);
}

/** Encode the next chunk. */
static void chunk_set_next(const struct arena *arena, union chunk *chunk, union chunk *next) {
	uintptr_t base = (uintptr_t)chunk + arena->size;
	chunk->next = (uintptr_t)next - base;
}

void arena_init(struct arena *arena, size_t align, size_t size) {
	bfs_assert(has_single_bit(align));
	bfs_assert((size & (align - 1)) == 0);

	if (align < alignof(union chunk)) {
		align = alignof(union chunk);
	}
	if (size < sizeof(union chunk)) {
		size = sizeof(union chunk);
	}
	bfs_assert((size & (align - 1)) == 0);

	arena->chunks = NULL;
	arena->nslabs = 0;
	arena->slabs = NULL;
	arena->align = align;
	arena->size = size;
}

/** Allocate a new slab. */
static int slab_alloc(struct arena *arena) {
	void **slabs = realloc(arena->slabs, sizeof_array(void *, arena->nslabs + 1));
	if (!slabs) {
		return -1;
	}
	arena->slabs = slabs;

	// Make the initial allocation size ~4K
	size_t size = 4096;
	if (size < arena->size) {
		size = arena->size;
	}
	// Trim off the excess
	size -= size % arena->size;
	// Double the size for every slab
	size <<= arena->nslabs;

	// Allocate the slab
	void *slab = zalloc(arena->align, size);
	if (!slab) {
		return -1;
	}

	// Fix the last chunk->next offset
	void *last = (char *)slab + size - arena->size;
	chunk_set_next(arena, last, arena->chunks);

	// We can rely on zero-initialized slabs, but others shouldn't
	sanitize_uninit(slab, size);

	arena->chunks = arena->slabs[arena->nslabs++] = slab;
	return 0;
}

void *arena_alloc(struct arena *arena) {
	if (!arena->chunks && slab_alloc(arena) != 0) {
		return NULL;
	}

	union chunk *chunk = arena->chunks;
	sanitize_alloc(chunk, arena->size);

	sanitize_init(chunk);
	arena->chunks = chunk_next(arena, chunk);
	sanitize_uninit(chunk, arena->size);

	return chunk;
}

void arena_free(struct arena *arena, void *ptr) {
	union chunk *chunk = ptr;
	chunk_set_next(arena, chunk, arena->chunks);
	arena->chunks = chunk;
	sanitize_free(chunk, arena->size);
}

void arena_destroy(struct arena *arena) {
	for (size_t i = 0; i < arena->nslabs; ++i) {
		free(arena->slabs[i]);
	}
	sanitize_uninit(arena);
}
