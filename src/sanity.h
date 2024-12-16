// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

/**
 * Sanitizer interface.
 */

#ifndef BFS_SANITY_H
#define BFS_SANITY_H

#include <stddef.h>

// Call macro(ptr, size) or macro(ptr, sizeof(*ptr))
#define SANITIZE_CALL(...) \
	SANITIZE_CALL_(__VA_ARGS__, )

#define SANITIZE_CALL_(macro, ptr, ...) \
	SANITIZE_CALL__(macro, ptr, __VA_ARGS__ sizeof(*(ptr)), )

#define SANITIZE_CALL__(macro, ptr, size, ...) \
	macro(ptr, size)

#if __SANITIZE_ADDRESS__
#  include <sanitizer/asan_interface.h>

/**
 * sanitize_alloc(ptr, size = sizeof(*ptr))
 *
 * Mark a memory region as allocated.
 */
#define sanitize_alloc(...) SANITIZE_CALL(__asan_unpoison_memory_region, __VA_ARGS__)

/**
 * sanitize_free(ptr, size = sizeof(*ptr))
 *
 * Mark a memory region as free.
 */
#define sanitize_free(...) SANITIZE_CALL(__asan_poison_memory_region, __VA_ARGS__)

/**
 * Adjust the size of an allocated region, for things like dynamic arrays.
 *
 * @ptr
 *         The memory region.
 * @old
 *         The previous usable size of the region.
 * @new
 *         The new usable size of the region.
 * @cap
 *         The total allocated capacity of the region.
 */
static inline void sanitize_resize(const void *ptr, size_t old, size_t new, size_t cap) {
	const char *beg = ptr;
	__sanitizer_annotate_contiguous_container(beg, beg + cap, beg + old, beg + new);
}

#else
#  define sanitize_alloc(...) ((void)0)
#  define sanitize_free(...) ((void)0)
#  define sanitize_resize(ptr, old, new, cap) ((void)0)
#endif

#if __SANITIZE_MEMORY__
#  include <sanitizer/msan_interface.h>

/**
 * sanitize_init(ptr, size = sizeof(*ptr))
 *
 * Mark a memory region as initialized.
 */
#define sanitize_init(...) SANITIZE_CALL(__msan_unpoison, __VA_ARGS__)

/**
 * sanitize_uninit(ptr, size = sizeof(*ptr))
 *
 * Mark a memory region as uninitialized.
 */
#define sanitize_uninit(...) SANITIZE_CALL(__msan_allocated_memory, __VA_ARGS__)

#else
#  define sanitize_init(...) ((void)0)
#  define sanitize_uninit(...) ((void)0)
#endif

/**
 * Initialize a variable, unless sanitizers would detect uninitialized uses.
 */
#if __SANITIZE_MEMORY__
#  define uninit(value)
#else
#  define uninit(value) = value
#endif

#endif // BFS_SANITY_H
