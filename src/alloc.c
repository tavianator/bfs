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
	free(arena->slabs);
	sanitize_uninit(arena);
}

void varena_init(struct varena *varena, size_t align, size_t min, size_t offset, size_t size) {
	varena->align = align;
	varena->offset = offset;
	varena->size = size;
	varena->narenas = 0;
	varena->arenas = NULL;

	// The smallest size class is at least as many as fit in the smallest
	// aligned allocation size
	size_t min_count = (flex_size(align, min, offset, size, 1) - offset + size - 1) / size;
	varena->shift = bit_width(min_count - 1);
}

/** Get the size class for the given array length. */
static size_t varena_size_class(struct varena *varena, size_t count) {
	// Since powers of two are common array lengths, make them the
	// (inclusive) upper bound for each size class
	return bit_width((count - !!count) >> varena->shift);
}

/** Get the exact size of a flexible struct. */
static size_t varena_exact_size(const struct varena *varena, size_t count) {
	return flex_size(varena->align, 0, varena->offset, varena->size, count);
}

/** Get the arena for the given array length. */
static struct arena *varena_get(struct varena *varena, size_t count) {
	size_t i = varena_size_class(varena, count);

	if (i >= varena->narenas) {
		size_t narenas = i + 1;
		struct arena *arenas = realloc(varena->arenas, sizeof_array(struct arena, narenas));
		if (!arenas) {
			return NULL;
		}

		for (size_t j = varena->narenas; j < narenas; ++j) {
			size_t shift = j + varena->shift;
			size_t size = varena_exact_size(varena, (size_t)1 << shift);
			arena_init(&arenas[j], varena->align, size);
		}

		varena->narenas = narenas;
		varena->arenas = arenas;
	}

	return &varena->arenas[i];
}

void *varena_alloc(struct varena *varena, size_t count) {
	struct arena *arena = varena_get(varena, count);
	if (!arena) {
		return NULL;
	}

	void *ret = arena_alloc(arena);
	if (!ret) {
		return NULL;
	}

	// Tell the sanitizers the exact size of the allocated struct
	sanitize_free(ret, arena->size);
	sanitize_alloc(ret, varena_exact_size(varena, count));

	return ret;
}

void *varena_realloc(struct varena *varena, void *ptr, size_t old_count, size_t new_count) {
	struct arena *new_arena = varena_get(varena, new_count);
	struct arena *old_arena = varena_get(varena, old_count);
	if (!new_arena) {
		return NULL;
	}

	size_t new_exact_size = varena_exact_size(varena, new_count);
	size_t old_exact_size = varena_exact_size(varena, old_count);

	if (new_arena == old_arena) {
		if (new_count < old_count) {
			sanitize_free((char *)ptr + new_exact_size, old_exact_size - new_exact_size);
		} else if (new_count > old_count) {
			sanitize_alloc((char *)ptr + old_exact_size, new_exact_size - old_exact_size);
		}
		return ptr;
	}

	void *ret = arena_alloc(new_arena);
	if (!ret) {
		return NULL;
	}

	size_t old_size = old_arena->size;
	sanitize_alloc((char *)ptr + old_exact_size, old_size - old_exact_size);

	size_t new_size = new_arena->size;
	size_t min_size = new_size < old_size ? new_size : old_size;
	memcpy(ret, ptr, min_size);

	arena_free(old_arena, ptr);
	sanitize_free((char *)ret + new_exact_size, new_size - new_exact_size);

	return ret;
}

void varena_free(struct varena *varena, void *ptr, size_t count) {
	struct arena *arena = varena_get(varena, count);
	arena_free(arena, ptr);
}

void varena_destroy(struct varena *varena) {
	for (size_t i = 0; i < varena->narenas; ++i) {
		arena_destroy(&varena->arenas[i]);
	}
	free(varena->arenas);
	sanitize_uninit(varena);
}
