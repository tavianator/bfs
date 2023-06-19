// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

/**
 * Memory allocation.
 */

#ifndef BFS_ALLOC_H
#define BFS_ALLOC_H

#include "config.h"
#include <stddef.h>

/** Round down to a multiple of an alignment. */
static inline size_t align_floor(size_t align, size_t size) {
	return size & ~(align - 1);
}

/** Round up to a multiple of an alignment. */
static inline size_t align_ceil(size_t align, size_t size) {
	return align_floor(align, size + align - 1);
}

/**
 * Saturating array size.
 *
 * @param align
 *         Array element alignment.
 * @param size
 *         Array element size.
 * @param count
 *         Array element count.
 * @return
 *         size * count, saturating to the maximum aligned value on overflow.
 */
static inline size_t array_size(size_t align, size_t size, size_t count) {
	size_t ret = size * count;
	return ret / size == count ? ret : ~(align - 1);
}

/** Saturating array sizeof. */
#define sizeof_array(type, count) \
	array_size(alignof(type), sizeof(type), count)

/** Size of a struct/union field. */
#define sizeof_member(type, member) \
	sizeof(((type *)NULL)->member)

/**
 * Saturating flexible struct size.
 *
 * @param align
 *         Struct alignment.
 * @param min
 *         Minimum struct size.
 * @param offset
 *         Flexible array member offset.
 * @param size
 *         Flexible array element size.
 * @param count
 *         Flexible array element count.
 * @return
 *         The size of the struct with count flexible array elements.  Saturates
 *         to the maximum aligned value on overflow.
 */
static inline size_t flex_size(size_t align, size_t min, size_t offset, size_t size, size_t count) {
	size_t ret = size * count;
	size_t overflow = ret / size != count;

	size_t extra = offset + align - 1;
	ret += extra;
	overflow |= ret < extra;
	ret |= -overflow;
	ret = align_floor(align, ret);

	// Make sure flex_sizeof(type, member, 0) >= sizeof(type), even if the
	// type has more padding than necessary for alignment
	if (min > align_ceil(align, offset)) {
		ret = ret < min ? min : ret;
	}

	return ret;
}

/**
 * Computes the size of a flexible struct.
 *
 * @param type
 *         The type of the struct containing the flexible array.
 * @param member
 *         The name of the flexible array member.
 * @param count
 *         The length of the flexible array.
 * @return
 *         The size of the struct with count flexible array elements.  Saturates
 *         to the maximum aligned value on overflow.
 */
#define sizeof_flex(type, member, count) \
	flex_size(alignof(type), sizeof(type), offsetof(type, member), sizeof_member(type, member[0]), count)

/**
 * General memory allocator.
 *
 * @param align
 *         The required alignment.
 * @param size
 *         The size of the allocation.
 * @return
 *         The allocated memory, or NULL on failure.
 */
void *alloc(size_t align, size_t size);

/**
 * Zero-initialized memory allocator.
 *
 * @param align
 *         The required alignment.
 * @param size
 *         The size of the allocation.
 * @return
 *         The allocated memory, or NULL on failure.
 */
void *zalloc(size_t align, size_t size);

/** Allocate memory for the given type. */
#define ALLOC(type) \
	(type *)alloc(alignof(type), sizeof(type))

/** Allocate zeroed memory for the given type. */
#define ZALLOC(type) \
	(type *)zalloc(alignof(type), sizeof(type))

/** Allocate memory for an array. */
#define ALLOC_ARRAY(type, count) \
	(type *)alloc(alignof(type), sizeof_array(type, count));

/** Allocate zeroed memory for an array. */
#define ZALLOC_ARRAY(type, count) \
	(type *)zalloc(alignof(type), sizeof_array(type, count));

/** Allocate memory for a flexible struct. */
#define ALLOC_FLEX(type, member, count) \
	(type *)alloc(alignof(type), sizeof_flex(type, member, count))

/** Allocate zeroed memory for a flexible struct. */
#define ZALLOC_FLEX(type, member, count) \
	(type *)zalloc(alignof(type), sizeof_flex(type, member, count))

#endif // BFS_ALLOC_H
