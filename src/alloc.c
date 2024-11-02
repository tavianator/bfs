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
		sanitize_alloc((char *)ptr + old_size, size);
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
	sanitize_free((char *)ret + old_size + size, new_size - old_size - size);
	errno = 0;
	return ret;
}

/**
 * A single contiguous slab in an arena.
 *
 * The allocatable chunks in a slab start at the beginning of the allocation,
 * so they can take advantage of the allocation's alignment.  A struct slab *
 * points to the metadata immediately after these chunks.  The metadata includes
 * a bitmap followed by a Fenwick tree[1] used to quickly find both free and
 * used chunks. Fenwick trees compute prefix sums efficiently:
 *
 *     query(i) = sum(bits[0:i])
 *
 * We use it to find the first free chunk:
 *
 *     min i such that query(i) < i
 *
 * as well as the next allocated chunk:
 *
 *     next(i) = min j such that query(i) < query(j)
 *
 * We actually store the tree with the granularity of N-bit words, so a full
 * bitmap has query(i) == N * i.
 *
 * [1]: https://en.wikipedia.org/wiki/Fenwick_tree
 */
struct slab {
	/** The beginning of the slab. */
	void *chunks;
	/** The size of each chunk. */
	size_t size;
	/** The number of words in the bitmap. */
	size_t length;
	/** The bitmap and the Fenwick tree. */
	size_t words[];
	// size_t bitmap[length];
	// size_t tree[length];
};

/** Poison a memory region. */
#define poison(...) \
	sanitize_uninit(__VA_ARGS__); \
	sanitize_free(__VA_ARGS__)

/** Unpoison a memory region. */
#define unpoison(...) \
	sanitize_alloc(__VA_ARGS__); \
	sanitize_init(__VA_ARGS__)

/** Allocate a new slab of the given height. */
_cold
static struct slab *slab_create(size_t align, size_t size, size_t order) {
	size_t length = ((size_t)1 << order) - 1;
	size_t chunks = length * SIZE_WIDTH;
	size_t nwords = 2 * length;

	size_t data_size = size_mul(size, chunks);

	size_t meta_offset = size_add(data_size, alignof(struct slab) - 1);
	meta_offset = align_floor(alignof(struct slab), meta_offset);

	size_t meta_size = sizeof_flex(struct slab, words, nwords);
	size_t total = size_add(meta_offset, meta_size);
	char *ptr = alloc(align, total);
	if (!ptr) {
		return NULL;
	}

	struct slab *slab = (struct slab *)(ptr + meta_offset);
	slab->chunks = ptr;
	slab->size = size;
	slab->length = length;
	for (size_t i = 0; i < nwords; ++i) {
		slab->words[i] = 0;
	}

	// Poison the whole slab so only the allocator can use it
	poison(ptr, total);
	return slab;
}

/** Get the first chunk in a slab. */
static void *slab_chunks(const struct slab *slab) {
	unpoison(&slab->chunks);
	void *ret = slab->chunks;
	poison(&slab->chunks);
	return ret;
}

/** Get the chunk size for a slab. */
static size_t slab_size(const struct slab *slab) {
	unpoison(&slab->size);
	size_t ret = slab->size;
	poison(&slab->size);
	return ret;
}

/** Get the length of the bitmap array. */
static size_t slab_length(const struct slab *slab) {
	unpoison(&slab->length);
	size_t ret = slab->length;
	poison(&slab_length);
	return ret;
}

/** Check if a chunk is from this slab. */
static bool slab_contains(const struct slab *slab, void *ptr) {
	// Avoid comparing pointers into different allocations
	uintptr_t addr = (uintptr_t)ptr;
	uintptr_t start = (uintptr_t)slab_chunks(slab);
	uintptr_t end = (uintptr_t)slab;
	return addr >= start && addr < end;
}

/** Get a word from a slab bitmap. */
static size_t bitmap_word(const struct slab *slab, size_t i) {
	bfs_assert(i < slab_length(slab));

	const size_t *word = &slab->words[i];
	unpoison(word);
	size_t ret = *word;
	poison(word);
	return ret;
}

/** Set a bit in a slab bitmap. */
static void bitmap_set(struct slab *slab, size_t i, size_t j) {
	bfs_assert(i < slab_length(slab));

	size_t *word = &slab->words[i];
	size_t bit = (size_t)1 << j;
	unpoison(word);
	bfs_assert(!(*word & bit));
	*word |= bit;
	poison(word);
}

/** Clear a bit in a slab bitmap. */
static void bitmap_clear(struct slab *slab, size_t i, size_t j) {
	bfs_assert(i < slab_length(slab));

	size_t *word = &slab->words[i];
	size_t bit = (size_t)1 << j;
	unpoison(word);
	bfs_assert(*word & bit);
	*word &= ~bit;
	poison(word);
}

/** Get the nth node of the Fenwick tree. */
static size_t fenwick_node(const struct slab *slab, size_t i) {
	size_t length = slab_length(slab);
	// Fenwick trees use 1-based indexing conventionally
	const size_t *tree = slab->words + length - 1;

	bfs_assert(i > 0 && i <= length);
	unpoison(&tree[i]);
	size_t ret = tree[i];
	poison(&tree[i]);
	return ret;
}

/** Query the Fenwick tree.  Returns sum(bits[0:N*i]). */
static size_t fenwick_query(const struct slab *slab, size_t i) {
	// (i & -i) isolates the least-significant bit in i.
	// https://en.wikipedia.org/wiki/Fenwick_tree#The_interrogation_tree
	size_t ret = 0;
	for (; i > 0; i -= i & -i) {
		ret += fenwick_node(slab, i);
	}
	return ret;
}

/** Update the Fenwick tree. */
static void fenwick_update(struct slab *slab, size_t i, ptrdiff_t delta) {
	size_t length = slab_length(slab);
	size_t *tree = slab->words + length - 1;

	// https://en.wikipedia.org/wiki/Fenwick_tree#The_update_tree
	for (++i; i <= length; i += i & -i) {
		unpoison(&tree[i]);
		tree[i] += delta;
		poison(&tree[i]);
	}
}

/** Binary search the Fenwick tree for the first free chunk. */
static size_t fenwick_search_free(struct slab *slab) {
	size_t low = 0;
	size_t bit = slab_length(slab) + 1;
	bfs_assert(has_single_bit(bit));

	// https://en.wikipedia.org/wiki/Fenwick_tree#The_search_tree
	do {
		bit >>= 1;
		size_t mid = low + bit;

		// tree[mid] == sum(bits[N*low:N*mid]), so a full node will have
		// tree[mid] == N * (mid - low) == N * bit
		size_t node = fenwick_node(slab, mid);
		if (node >= bit * SIZE_WIDTH) {
			low = mid;
		}
	} while (bit > 1);

	return low;
}

/** Binary search the Fenwick tree for a specific rank. */
static size_t fenwick_search_next(struct slab *slab, size_t n) {
	size_t low = 0;
	size_t bit = slab_length(slab) + 1;
	bfs_assert(has_single_bit(bit));

	do {
		bit >>= 1;
		size_t mid = low + bit;
		size_t node = fenwick_node(slab, mid);
		if (node <= n) {
			low = mid;
			n -= node;
		}
	} while (bit > 1);

	return low;
}

/** Get the chunk for a bitmap index. */
static void *nth_chunk(struct slab *slab, size_t i, size_t j) {
	bfs_assert(i < slab_length(slab));
	char *chunks = slab_chunks(slab);
	size_t size = slab_size(slab);
	return chunks + (SIZE_WIDTH * i + j) * size;
}

/** Allocate a chunk from a slab. */
static void *slab_alloc(struct slab *slab) {
	size_t i = fenwick_search_free(slab);
	if (i >= slab_length(slab)) {
		return NULL;
	}

	size_t word = bitmap_word(slab, i);
	bfs_assume(word != SIZE_MAX);
	size_t j = trailing_ones(word);
	bitmap_set(slab, i, j);
	fenwick_update(slab, i, 1);

	void *ret = nth_chunk(slab, i, j);
	sanitize_alloc(ret, slab_size(slab));
	return ret;
}

/** Get the bitmap index for a chunk. */
static size_t chunk_index(struct slab *slab, void *ptr) {
	bfs_assert(slab_contains(slab, ptr));
	char *start = slab_chunks(slab);
	size_t size = slab_size(slab);
	return ((char *)ptr - start) / size;
}

/** Free a chunk in a slab. */
static void slab_free(struct slab *slab, void *ptr) {
	size_t i = chunk_index(slab, ptr);
	size_t j = i % SIZE_WIDTH;
	i /= SIZE_WIDTH;

	bitmap_clear(slab, i, j);
	fenwick_update(slab, i, -1);

	poison(ptr, slab_size(slab));
}

void *slab_next(struct slab *slab, void *ptr) {
	// Find an allocated chunk after the given pointer
	size_t min = 0;
	if (ptr) {
		min = 1 + chunk_index(slab, ptr);
	}

	size_t i = min / SIZE_WIDTH;
	size_t j = min % SIZE_WIDTH;
	size_t length = slab_length(slab);
	if (i >= length) {
		return NULL;
	}

	// Check for a 1 at bit j or higher
	size_t word = bitmap_word(slab, i);
	size_t bit = (size_t)1 << j;
	j = trailing_zeros(word & -bit);

	if (j >= SIZE_WIDTH) {
		// None in the same word, query the Fenwick tree
		size_t rank = fenwick_query(slab, i + 1);
		i = fenwick_search_next(slab, rank);
		if (i >= length) {
			return NULL;
		}

		word = bitmap_word(slab, i);
		j = trailing_zeros(word);
		bfs_assert(j < SIZE_WIDTH);
	}

	return nth_chunk(slab, i, j);
}

/** Free a whole slab. */
static void slab_destroy(struct slab *slab) {
	unpoison(&slab->chunks);
	free(slab->chunks);
}

void arena_init(struct arena *arena, size_t align, size_t size) {
	bfs_assert(has_single_bit(align));
	bfs_assert(is_aligned(align, size));

	arena->nslabs = 0;
	arena->slabs = NULL;
	arena->align = align;
	arena->size = size;
}

void *arena_alloc(struct arena *arena) {
	// Try the largest slab first
	for (size_t i = arena->nslabs; i-- > 0;) {
		void *ret = slab_alloc(arena->slabs[i]);
		if (ret) {
			return ret;
		}
	}

	// All slabs are full, make a new one
	struct slab **slab = RESERVE(struct slab *, &arena->slabs, &arena->nslabs);
	if (!slab) {
		return NULL;
	}

	*slab = slab_create(arena->align, arena->size, arena->nslabs);
	if (!*slab) {
		--arena->nslabs;
		return NULL;
	}

	return slab_alloc(*slab);
}

/** Check if a pointer comes from this arena. */
static bool arena_contains(const struct arena *arena, void *ptr) {
	for (size_t i = arena->nslabs; i-- > 0;) {
		if (slab_contains(arena->slabs[i], ptr)) {
			return true;
		}
	}

	return false;
}

void arena_free(struct arena *arena, void *ptr) {
	bfs_assert(arena_contains(arena, ptr));

	for (size_t i = arena->nslabs; i-- > 0;) {
		struct slab *slab = arena->slabs[i];
		if (slab_contains(slab, ptr)) {
			slab_free(slab, ptr);
			break;
		}
	}
}

void arena_clear(struct arena *arena) {
	for (size_t i = 0; i < arena->nslabs; ++i) {
		slab_destroy(arena->slabs[i]);
	}
	free(arena->slabs);

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

/** Get the arena containing a given pointer. */
static struct arena *varena_find(struct varena *varena, void *ptr) {
	for (size_t i = 0; i < varena->narenas; ++i) {
		struct arena *arena = &varena->arenas[i];
		if (arena_contains(arena, ptr)) {
			return arena;
		}
	}

	bfs_abort("No arena contains %p", ptr);
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

void *varena_realloc(struct varena *varena, void *ptr, size_t count) {
	struct arena *new_arena = varena_get(varena, count);
	struct arena *old_arena = varena_find(varena, ptr);
	if (!new_arena) {
		return NULL;
	}

	size_t new_size = new_arena->size;
	size_t new_exact_size = varena_exact_size(varena, count);

	void *ret;
	if (new_arena == old_arena) {
		ret = ptr;
		goto done;
	}

	ret = arena_alloc(new_arena);
	if (!ret) {
		return NULL;
	}

	size_t old_size = old_arena->size;
	sanitize_alloc(ptr, old_size);

	size_t min_size = new_size < old_size ? new_size : old_size;
	memcpy(ret, ptr, min_size);

	arena_free(old_arena, ptr);
done:
	sanitize_free(ret, new_size);
	sanitize_alloc(ret, new_exact_size);
	return ret;
}

void *varena_grow(struct varena *varena, void *ptr, size_t *count) {
	size_t old_count = *count;

	// Round up to the limit of the current size class.  If we're already at
	// the limit, go to the next size class.
	size_t new_shift = varena_size_class(varena, old_count + 1) + varena->shift;
	size_t new_count = (size_t)1 << new_shift;

	ptr = varena_realloc(varena, ptr, new_count);
	if (ptr) {
		*count = new_count;
	}
	return ptr;
}

void varena_free(struct varena *varena, void *ptr) {
	struct arena *arena = varena_find(varena, ptr);
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
