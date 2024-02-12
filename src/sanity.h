// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

/**
 * Sanitizer interface.
 */

#ifndef BFS_SANITY_H
#define BFS_SANITY_H

#include "config.h"
#include <stddef.h>

#if __has_feature(address_sanitizer) || defined(__SANITIZE_ADDRESS__)
#  define SANITIZE_ADDRESS true
#endif

#if __has_feature(memory_sanitizer) || defined(__SANITIZE_MEMORY__)
#  define SANITIZE_MEMORY true
#endif

#if __has_feature(thread_sanitizer) || defined(__SANITIZE_THREAD__)
#  define SANITIZE_THREAD true
#endif

// Call macro(ptr, size) or macro(ptr, sizeof(*ptr))
#define SANITIZE_CALL(...) \
	SANITIZE_CALL_(__VA_ARGS__, )

#define SANITIZE_CALL_(macro, ptr, ...) \
	SANITIZE_CALL__(macro, ptr, __VA_ARGS__ sizeof(*(ptr)), )

#define SANITIZE_CALL__(macro, ptr, size, ...) \
	macro(ptr, size)

#if SANITIZE_ADDRESS
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

#else
#  define sanitize_alloc sanitize_uninit
#  define sanitize_free sanitize_uninit
#endif

#if SANITIZE_MEMORY
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
#  define sanitize_init(...) SANITIZE_CALL(sanitize_ignore, __VA_ARGS__)
#  define sanitize_uninit(...) SANITIZE_CALL(sanitize_ignore, __VA_ARGS__)
#endif

/**
 * Squelch unused variable warnings when not sanitizing.
 */
#define sanitize_ignore(ptr, size) ((void)(ptr), (void)(size))

/**
 * Initialize a variable, unless sanitizers would detect uninitialized uses.
 */
#if SANITIZE_MEMORY
#  define uninit(value)
#else
#  define uninit(value) = value
#endif

#endif // BFS_SANITY_H
