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

/**
 * This is an implementation of a "qp trie," as documented at
 * https://dotat.at/prog/qp/README.html
 *
 * An uncompressed trie over the dataset {AAAA, AADD, ABCD, DDAA, DDDD} would
 * look like
 *
 *       A    A    A    A
 *     *--->*--->*--->*--->$
 *     |    |    | D    D
 *     |    |    +--->*--->$
 *     |    | B    C    D
 *     |    +--->*--->*--->$
 *     | D    D    A    A
 *     +--->*--->*--->*--->$
 *               | D    D
 *               +--->*--->$
 *
 * A compressed (PATRICIA) trie collapses internal nodes that have only a single
 * child, like this:
 *
 *       A    A    AA
 *     *--->*--->*---->$
 *     |    |    | DD
 *     |    |    +---->$
 *     |    | BCD
 *     |    +----->$
 *     | DD    AA
 *     +---->*---->$
 *           | DD
 *           +---->$
 *
 * The nodes can be compressed further by dropping the actual compressed
 * sequences from the nodes, storing it only in the leaves.  This is the
 * technique applied in QP tries, and the crit-bit trees that inspired them
 * (https://cr.yp.to/critbit.html).  Only the index to test, and the values to
 * branch on, need to be stored in each node.
 *
 *       A    A    A
 *     0--->1--->2--->AAAA
 *     |    |    | D
 *     |    |    +--->AADD
 *     |    | B
 *     |    +--->ABCD
 *     | D    A
 *     +--->2--->DDAA
 *          | D
 *          +--->DDDD
 *
 * Nodes are represented very compactly.  Rather than a dense array of children,
 * a sparse array of only the non-NULL children directly follows the node in
 * memory.  A bitmap is used to track which children exist; the index of a child
 * i is found by counting the number of bits below bit i that are set.  A tag
 * bit is used to tell pointers to internal nodes apart from pointers to leaves.
 *
 * This implementation tests a whole nibble (half byte/hex digit) at every
 * branch, so the bitmap takes up 16 bits.  The remainder of a machine word is
 * used to hold the offset, which severely constrains its range on 32-bit
 * platforms.  As a workaround, we store relative instead of absolute offsets,
 * and insert intermediate singleton "jump" nodes when necessary.
 */

#include "trie.h"
#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if CHAR_BIT != 8
#	error "This trie implementation assumes 8-bit bytes."
#endif

/** Number of bits for the sparse array bitmap, aka the range of a nibble. */
#define BITMAP_BITS 16
/** The number of remaining bits in a word, to hold the offset. */
#define OFFSET_BITS (sizeof(size_t)*CHAR_BIT - BITMAP_BITS)
/** The highest representable offset (only 64k on a 32-bit architecture). */
#define OFFSET_MAX (((size_t)1 << OFFSET_BITS) - 1)

/**
 * An internal node of the trie.
 */
struct trie_node {
	/**
	 * A bitmap that hold which indices exist in the sparse children array.
	 * Bit i will be set if a child exists at logical index i, and its index
	 * into the array will be popcount(bitmap & ((1 << i) - 1)).
	 */
	size_t bitmap : BITMAP_BITS;

	/**
	 * The offset into the key in nibbles.  This is relative to the parent
	 * node, to support offsets larger than OFFSET_MAX.
	 */
	size_t offset : OFFSET_BITS;

	/**
	 * Flexible array of children.  Each pointer uses the lowest bit as a
	 * tag to distinguish internal nodes from leaves.  This is safe as long
	 * as all dynamic allocations are aligned to more than a single byte.
	 */
	uintptr_t children[];
};

/** Check if an encoded pointer is to a leaf. */
static bool trie_is_leaf(uintptr_t ptr) {
	return ptr & 1;
}

/** Decode a pointer to a leaf. */
static struct trie_leaf *trie_decode_leaf(uintptr_t ptr) {
	assert(trie_is_leaf(ptr));
	return (struct trie_leaf *)(ptr ^ 1);
}

/** Encode a pointer to a leaf. */
static uintptr_t trie_encode_leaf(const struct trie_leaf *leaf) {
	uintptr_t ptr = (uintptr_t)leaf ^ 1;
	assert(trie_is_leaf(ptr));
	return ptr;
}

/** Decode a pointer to an internal node. */
static struct trie_node *trie_decode_node(uintptr_t ptr) {
	assert(!trie_is_leaf(ptr));
	return (struct trie_node *)ptr;
}

/** Encode a pointer to an internal node. */
static uintptr_t trie_encode_node(const struct trie_node *node) {
	uintptr_t ptr = (uintptr_t)node;
	assert(!trie_is_leaf(ptr));
	return ptr;
}

void trie_init(struct trie *trie) {
	trie->root = 0;
}

/** Compute the popcount (Hamming weight) of a bitmap. */
static unsigned int trie_popcount(unsigned int n) {
#if __POPCNT__
	// Use the x86 instruction if we have it.  Otherwise, GCC generates a
	// library call, so use the below implementation instead.
	return __builtin_popcount(n);
#else
	// See https://en.wikipedia.org/wiki/Hamming_weight#Efficient_implementation
	n -= (n >> 1) & 0x5555;
	n = (n & 0x3333) + ((n >> 2) & 0x3333);
	n = (n + (n >> 4)) & 0x0F0F;
	n = (n + (n >> 8)) & 0xFF;
	return n;
#endif
}

/** Extract the nibble at a certain offset from a byte sequence. */
static unsigned char trie_key_nibble(const void *key, size_t offset) {
	const unsigned char *bytes = key;
	size_t byte = offset >> 1;

	// A branchless version of
	// if (offset & 1) {
	//         return bytes[byte] >> 4;
	// } else {
	//         return bytes[byte] & 0xF;
	// }
	unsigned int shift = (offset & 1) << 2;
	return (bytes[byte] >> shift) & 0xF;
}

/**
 * Finds a leaf in the trie that matches the key at every branch.  If the key
 * exists in the trie, the representative will match the searched key.  But
 * since only branch points are tested, it can be different from the key.  In
 * that case, the first mismatch between the key and the representative will be
 * the depth at which to make a new branch to insert the key.
 */
static struct trie_leaf *trie_representative(const struct trie *trie, const void *key, size_t length) {
	uintptr_t ptr = trie->root;
	if (!ptr) {
		return NULL;
	}

	size_t offset = 0;
	while (!trie_is_leaf(ptr)) {
		struct trie_node *node = trie_decode_node(ptr);
		offset += node->offset;

		unsigned char nibble = 0;
		if ((offset >> 1) < length) {
			nibble = trie_key_nibble(key, offset);
		}
		unsigned int bit = 1U << nibble;
		unsigned int index = 0;
		if (node->bitmap & bit) {
			index = trie_popcount(node->bitmap & (bit - 1));
		}
		ptr = node->children[index];
	}

	return trie_decode_leaf(ptr);
}

struct trie_leaf *trie_find_str(const struct trie *trie, const char *key) {
	return trie_find_mem(trie, key, strlen(key) + 1);
}

struct trie_leaf *trie_find_mem(const struct trie *trie, const void *key, size_t length) {
	struct trie_leaf *rep = trie_representative(trie, key, length);
	if (rep && rep->length == length && memcmp(rep->key, key, length) == 0) {
		return rep;
	} else {
		return NULL;
	}
}

/** Create a new leaf, holding a copy of the given key. */
static struct trie_leaf *new_trie_leaf(const void *key, size_t length) {
	struct trie_leaf *leaf = malloc(sizeof(*leaf) + length);
	if (leaf) {
		leaf->value = NULL;
		leaf->length = length;
		memcpy(leaf->key, key, length);
	}
	return leaf;
}

/** Compute the size of a trie node with a certain number of children. */
static size_t trie_node_size(unsigned int size) {
	// Empty nodes aren't supported
	assert(size > 0);
	// Node size must be a power of two
	assert((size & (size - 1)) == 0);

	return sizeof(struct trie_node) + size*sizeof(uintptr_t);
}

/** Find the offset of the first nibble that differs between two keys. */
static size_t trie_key_mismatch(const void *key1, const void *key2, size_t length) {
	const unsigned char *bytes1 = key1;
	const unsigned char *bytes2 = key2;
	size_t i = 0;
	size_t offset = 0;
	const size_t chunk = sizeof(size_t);

	for (; i + chunk <= length; i += chunk) {
		if (memcmp(bytes1 + i, bytes2 + i, chunk) != 0) {
			break;
		}
	}

	for (; i < length; ++i) {
		unsigned char b1 = bytes1[i], b2 = bytes2[i];
		if (b1 != b2) {
			offset = (b1 & 0xF) == (b2 & 0xF);
			break;
		}
	}

	offset |= i << 1;
	return offset;
}

/**
 * Insert a key into a node.  The node must not have a child in that position
 * already.  Effectively takes a subtrie like this:
 *
 *     ptr
 *      |
 *      v X
 *      *--->...
 *      | Z
 *      +--->...
 *
 * and transforms it to:
 *
 *     ptr
 *      |
 *      v X
 *      *--->...
 *      | Y
 *      +--->key
 *      | Z
 *      +--->...
 */
static struct trie_leaf *trie_node_insert(uintptr_t *ptr, const void *key, size_t length, size_t offset) {
	struct trie_node *node = trie_decode_node(*ptr);
	unsigned int size = trie_popcount(node->bitmap);

	// Double the capacity every power of two
	if ((size & (size - 1)) == 0) {
		node = realloc(node, trie_node_size(2*size));
		if (!node) {
			return NULL;
		}
		*ptr = trie_encode_node(node);
	}

	struct trie_leaf *leaf = new_trie_leaf(key, length);
	if (!leaf) {
		return NULL;
	}

	unsigned char nibble = trie_key_nibble(key, offset);
	unsigned int bit = 1U << nibble;

	// The child must not already be present
	assert(!(node->bitmap & bit));
	node->bitmap |= bit;

	unsigned int index = trie_popcount(node->bitmap & (bit - 1));
	uintptr_t *child = node->children + index;
	if (index < size) {
		memmove(child + 1, child, (size - index)*sizeof(*child));
	}
	*child = trie_encode_leaf(leaf);
	return leaf;
}

/**
 * When the current offset exceeds OFFSET_MAX, insert "jump" nodes that bridge
 * the gap.  This function takes a subtrie like this:
 *
 *     ptr
 *      |
 *      v
 *      *--->rep
 *
 * and changes it to:
 *
 *     ptr  ret
 *      |    |
 *      v    v
 *      *--->*--->rep
 *
 * so that a new key can be inserted like:
 *
 *     ptr  ret
 *      |    |
 *      v    v X
 *      *--->*--->rep
 *           | Y
 *           +--->key
 */
static uintptr_t *trie_jump(uintptr_t *ptr, const char *key, size_t *offset) {
	// We only ever need to jump to leaf nodes, since internal nodes are
	// guaranteed to be within OFFSET_MAX anyway
	assert(trie_is_leaf(*ptr));

	struct trie_node *node = malloc(trie_node_size(1));
	if (!node) {
		return NULL;
	}

	*offset += OFFSET_MAX;
	node->offset = *offset;

	unsigned char nibble = trie_key_nibble(key, *offset);
	node->bitmap = 1 << nibble;

	node->children[0] = *ptr;
	*ptr = trie_encode_node(node);
	return node->children;
}

/**
 * Split a node in the trie.  Changes a subtrie like this:
 *
 *     ptr
 *      |
 *      v
 *      *...>--->rep
 *
 * into this:
 *
 *     ptr
 *      |
 *      v X
 *      *--->*...>--->rep
 *      | Y
 *      +--->key
 */
static struct trie_leaf *trie_split(uintptr_t *ptr, const void *key, size_t length, struct trie_leaf *rep, size_t offset, size_t mismatch) {
	unsigned char key_nibble = trie_key_nibble(key, mismatch);
	unsigned char rep_nibble = trie_key_nibble(rep->key, mismatch);
	assert(key_nibble != rep_nibble);

	struct trie_node *node = malloc(trie_node_size(2));
	if (!node) {
		return NULL;
	}

	struct trie_leaf *leaf = new_trie_leaf(key, length);
	if (!leaf) {
		free(node);
		return NULL;
	}

	node->bitmap = (1 << key_nibble) | (1 << rep_nibble);

	size_t delta = mismatch - offset;
	if (!trie_is_leaf(*ptr)) {
		struct trie_node *child = trie_decode_node(*ptr);
		child->offset -= delta;
	}
	node->offset = delta;

	unsigned int key_index = key_nibble > rep_nibble;
	node->children[key_index] = trie_encode_leaf(leaf);
	node->children[key_index ^ 1] = *ptr;
	*ptr = trie_encode_node(node);
	return leaf;
}

struct trie_leaf *trie_insert_str(struct trie *trie, const char *key) {
	return trie_insert_mem(trie, key, strlen(key) + 1);
}

struct trie_leaf *trie_insert_mem(struct trie *trie, const void *key, size_t length) {
	struct trie_leaf *rep = trie_representative(trie, key, length);
	if (!rep) {
		struct trie_leaf *leaf = new_trie_leaf(key, length);
		if (leaf) {
			trie->root = trie_encode_leaf(leaf);
		}
		return leaf;
	}

	size_t limit = length < rep->length ? length : rep->length;
	size_t mismatch = trie_key_mismatch(key, rep->key, limit);
	if ((mismatch >> 1) >= length) {
		return rep;
	}

	size_t offset = 0;
	uintptr_t *ptr = &trie->root;
	while (!trie_is_leaf(*ptr)) {
		struct trie_node *node = trie_decode_node(*ptr);
		if (offset + node->offset > mismatch) {
			break;
		}
		offset += node->offset;

		unsigned char nibble = trie_key_nibble(key, offset);
		unsigned int bit = 1U << nibble;
		if (node->bitmap & bit) {
			assert(offset < mismatch);
			unsigned int index = trie_popcount(node->bitmap & (bit - 1));
			ptr = node->children + index;
		} else {
			assert(offset == mismatch);
			return trie_node_insert(ptr, key, length, offset);
		}
	}

	while (mismatch - offset > OFFSET_MAX) {
		ptr = trie_jump(ptr, key, &offset);
		if (!ptr) {
			return NULL;
		}
	}

	return trie_split(ptr, key, length, rep, offset, mismatch);
}

/** Free an encoded pointer to a node. */
static void free_trie_ptr(uintptr_t ptr) {
	if (trie_is_leaf(ptr)) {
		free(trie_decode_leaf(ptr));
	} else {
		struct trie_node *node = trie_decode_node(ptr);
		size_t size = trie_popcount(node->bitmap);
		for (size_t i = 0; i < size; ++i) {
			free_trie_ptr(node->children[i]);
		}
		free(node);
	}
}

void trie_destroy(struct trie *trie) {
	if (trie->root) {
		free_trie_ptr(trie->root);
	}
}
