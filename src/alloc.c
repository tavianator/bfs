// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include "alloc.h"

#include "bfs.h"
#include "bit.h"
#include "diag.h"
#include "sanity.h"

#include <errno.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

/** The largest possible allocation size. */
#if PTRDIFF_MAX < SIZE_MAX / 2
#  define ALLOC_MAX ((size_t)PTRDIFF_MAX)
#else
#  define ALLOC_MAX (SIZE_MAX / 2)
#endif

/** posix_memalign() wrapper. */
static void *xmemalign(size_t align, size_t size) {
	bfs_assert(has_single_bit(align));
	bfs_assert(align >= sizeof(void *));

	// Since https://www.open-std.org/jtc1/sc22/wg14/www/docs/n2072.htm,
	// aligned_alloc() doesn't require the size to be a multiple of align.
	// But the sanitizers don't know about that yet, so always use
	// posix_memalign().
	void *ptr = NULL;
	errno = posix_memalign(&ptr, align, size);
	return ptr;
}

void *alloc(size_t align, size_t size) {
	bfs_assert(has_single_bit(align));

	if (size > ALLOC_MAX) {
		errno = EOVERFLOW;
		return NULL;
	}

	if (align <= alignof(max_align_t)) {
		return malloc(size);
	} else {
		return xmemalign(align, size);
	}
}

void *zalloc(size_t align, size_t size) {
	bfs_assert(has_single_bit(align));

	if (size > ALLOC_MAX) {
		errno = EOVERFLOW;
		return NULL;
	}

	if (align <= alignof(max_align_t)) {
		return calloc(1, size);
	}

	void *ret = xmemalign(align, size);
	if (ret) {
		memset(ret, 0, size);
	}
	return ret;
}

void *xrealloc(void *ptr, size_t align, size_t old_size, size_t new_size) {
	bfs_assert(has_single_bit(align));

	if (new_size == 0) {
		free(ptr);
		return NULL;
	} else if (new_size > ALLOC_MAX) {
		errno = EOVERFLOW;
		return NULL;
	}

	if (align <= alignof(max_align_t)) {
		return realloc(ptr, new_size);
	}

	// There is no aligned_realloc(), so reallocate and copy manually
	void *ret = xmemalign(align, new_size);
	if (!ret) {
		return NULL;
	}

	size_t min_size = old_size < new_size ? old_size : new_size;
	if (min_size) {
		memcpy(ret, ptr, min_size);
	}

	free(ptr);
	return ret;
}

void *reserve(void *ptr, size_t align, size_t size, size_t count) {
	// No need to overflow-check the current size
	size_t old_size = size * count;

	// Capacity is doubled every power of two, from 0→1, 1→2, 2→4, etc.
	// If we stayed within the same size class, reuse ptr.
	if (count & (count - 1)) {
		// Tell sanitizers about the new array element
		sanitize_resize(ptr, old_size, old_size + size, bit_ceil(count) * size);
		errno = 0;
		return ptr;
	}

	// No need to overflow-check; xrealloc() will fail before we overflow
	size_t new_size = count ? 2 * old_size : size;
	void *ret = xrealloc(ptr, align, old_size, new_size);
	if (!ret) {
		// errno is used to communicate success/failure to the RESERVE() macro
		bfs_assert(errno != 0);
		return ptr;
	}

	// Pretend we only allocated one more element
	sanitize_resize(ret, new_size, old_size + size, new_size);
	errno = 0;
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
	bfs_assert(is_aligned(align, size));

	if (align < alignof(union chunk)) {
		align = alignof(union chunk);
	}
	if (size < sizeof(union chunk)) {
		size = sizeof(union chunk);
	}
	bfs_assert(is_aligned(align, size));

	arena->chunks = NULL;
	arena->nslabs = 0;
	arena->slabs = NULL;
	arena->align = align;
	arena->size = size;
}

/** Allocate a new slab. */
_cold
static int slab_alloc(struct arena *arena) {
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

	// Grow the slab array
	void **pslab = RESERVE(void *, &arena->slabs, &arena->nslabs);
	if (!pslab) {
		free(slab);
		return -1;
	}

	// Fix the last chunk->next offset
	void *last = (char *)slab + size - arena->size;
	chunk_set_next(arena, last, arena->chunks);

	// We can rely on zero-initialized slabs, but others shouldn't
	sanitize_uninit(slab, size);

	arena->chunks = *pslab = slab;
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
	sanitize_uninit(chunk, arena->size);
	sanitize_free(chunk, arena->size);
}

void arena_clear(struct arena *arena) {
	for (size_t i = 0; i < arena->nslabs; ++i) {
		free(arena->slabs[i]);
	}
	free(arena->slabs);

	arena->chunks = NULL;
	arena->nslabs = 0;
	arena->slabs = NULL;
}

void arena_destroy(struct arena *arena) {
	arena_clear(arena);
	sanitize_uninit(arena);
}

void varena_init(struct varena *varena, size_t align, size_t offset, size_t size) {
	varena->align = align;
	varena->offset = offset;
	varena->size = size;
	varena->narenas = 0;
	varena->arenas = NULL;

	// The smallest size class is at least as many as fit in the smallest
	// aligned allocation size
	size_t min_count = (flex_size(align, offset, size, 1) - offset + size - 1) / size;
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
	return flex_size(varena->align, varena->offset, varena->size, count);
}

/** Get the arena for the given array length. */
static struct arena *varena_get(struct varena *varena, size_t count) {
	size_t i = varena_size_class(varena, count);

	while (i >= varena->narenas) {
		size_t j = varena->narenas;
		struct arena *arena = RESERVE(struct arena, &varena->arenas, &varena->narenas);
		if (!arena) {
			return NULL;
		}

		size_t shift = j + varena->shift;
		size_t size = varena_exact_size(varena, (size_t)1 << shift);
		arena_init(arena, varena->align, size);
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
	sanitize_resize(ret, arena->size, varena_exact_size(varena, count), arena->size);

	return ret;
}

void *varena_realloc(struct varena *varena, void *ptr, size_t old_count, size_t new_count) {
	struct arena *new_arena = varena_get(varena, new_count);
	struct arena *old_arena = varena_get(varena, old_count);
	if (!new_arena) {
		return NULL;
	}

	size_t old_size = old_arena->size;
	size_t new_size = new_arena->size;

	if (new_arena == old_arena) {
		sanitize_resize(ptr,
			varena_exact_size(varena, old_count),
			varena_exact_size(varena, new_count),
			new_size);
		return ptr;
	}

	void *ret = arena_alloc(new_arena);
	if (!ret) {
		return NULL;
	}

	// Non-sanitized builds don't bother computing exact sizes, and just use
	// the potentially-larger arena size for each size class instead.  To
	// allow the below memcpy() to work with the less-precise sizes, expand
	// the old allocation to its full capacity.
	sanitize_resize(ptr, varena_exact_size(varena, old_count), old_size, old_size);

	size_t min_size = new_size < old_size ? new_size : old_size;
	memcpy(ret, ptr, min_size);

	arena_free(old_arena, ptr);

	sanitize_resize(ret, new_size, varena_exact_size(varena, new_count), new_size);
	return ret;
}

void *varena_grow(struct varena *varena, void *ptr, size_t *count) {
	size_t old_count = *count;

	// Round up to the limit of the current size class.  If we're already at
	// the limit, go to the next size class.
	size_t new_shift = varena_size_class(varena, old_count + 1) + varena->shift;
	size_t new_count = (size_t)1 << new_shift;

	ptr = varena_realloc(varena, ptr, old_count, new_count);
	if (ptr) {
		*count = new_count;
	}
	return ptr;
}

void varena_free(struct varena *varena, void *ptr, size_t count) {
	struct arena *arena = varena_get(varena, count);
	arena_free(arena, ptr);
}

void varena_clear(struct varena *varena) {
	for (size_t i = 0; i < varena->narenas; ++i) {
		arena_clear(&varena->arenas[i]);
	}
}

void varena_destroy(struct varena *varena) {
	for (size_t i = 0; i < varena->narenas; ++i) {
		arena_destroy(&varena->arenas[i]);
	}
	free(varena->arenas);
	sanitize_uninit(varena);
}
