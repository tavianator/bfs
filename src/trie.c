// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

/**
 * This is an implementation of a "qp trie," as documented at
 * https://dotat.at/prog/qp/README.html
 *
 * An uncompressed trie over the dataset {AAAA, AADD, ABCD, DDAA, DDDD} would
 * look like
 *
 *       A    A    A    A
 *     ●───→●───→●───→●───→○
 *     │    │    │ D    D
 *     │    │    └───→●───→○
 *     │    │ B    C    D
 *     │    └───→●───→●───→○
 *     │ D    D    A    A
 *     └───→●───→●───→●───→○
 *               │ D    D
 *               └───→●───→○
 *
 * A compressed (PATRICIA) trie collapses internal nodes that have only a single
 * child, like this:
 *
 *       A    A    AA
 *     ●───→●───→●────→○
 *     │    │    │ DD
 *     │    │    └────→○
 *     │    │ BCD
 *     │    └─────→○
 *     │ DD    AA
 *     └────→●────→○
 *           │ DD
 *           └────→○
 *
 * The nodes can be compressed further by dropping the actual compressed
 * sequences from the nodes, storing it only in the leaves.  This is the
 * technique applied in QP tries, and the crit-bit trees that inspired them
 * (https://cr.yp.to/critbit.html).  Only the index to test, and the values to
 * branch on, need to be stored in each node.
 *
 *       A    A    A
 *     0───→1───→2───→AAAA
 *     │    │    │ D
 *     │    │    └───→AADD
 *     │    │ B
 *     │    └───→ABCD
 *     │ D    A
 *     └───→2───→DDAA
 *          │ D
 *          └───→DDDD
 *
 * Nodes are represented very compactly.  Rather than a dense array of children,
 * a sparse array of only the non-NULL children directly follows the node in
 * memory.  A bitmap is used to track which children exist.
 *
 * ┌────────────┐
 * │       [4] [3] [2][1][0] ←─ children
 * │        ↓   ↓   ↓  ↓  ↓
 * │       14  10   6  3  0  ←─ sparse index
 * │        ↓   ↓   ↓  ↓  ↓
 * │       0100010001001001  ←─ bitmap
 * │
 * │ To convert a sparse index to a dense index, mask off the bits above it, and
 * │ count the remaining bits.
 * │
 * │           10            ←─ sparse index
 * │            ↓
 * │       0000001111111111  ←─ mask
 * │     & 0100010001001001  ←─ bitmap
 * │       ────────────────
 * │     = 0000000001001001
 * │                └──┼──┘
 * │                  [3]    ←─ dense index
 * └───────────────────┘
 *
 * This implementation tests a whole nibble (half byte/hex digit) at every
 * branch, so the bitmap takes up 16 bits.  The remainder of a machine word is
 * used to hold the offset, which severely constrains its range on 32-bit
 * platforms.  As a workaround, we store relative instead of absolute offsets,
 * and insert intermediate singleton "jump" nodes when necessary.
 */

#include "trie.h"
#include "alloc.h"
#include "bit.h"
#include "config.h"
#include "diag.h"
#include "list.h"
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

bfs_static_assert(CHAR_WIDTH == 8);

#if BFS_USE_TARGET_CLONES && (__i386__ || __x86_64__)
#  define TARGET_CLONES_POPCNT __attribute__((target_clones("popcnt", "default")))
#else
#  define TARGET_CLONES_POPCNT
#endif

/** Number of bits for the sparse array bitmap, aka the range of a nibble. */
#define BITMAP_WIDTH 16
/** The number of remaining bits in a word, to hold the offset. */
#define OFFSET_WIDTH (SIZE_WIDTH - BITMAP_WIDTH)
/** The highest representable offset (only 64k on a 32-bit architecture). */
#define OFFSET_MAX (((size_t)1 << OFFSET_WIDTH) - 1)

/**
 * An internal node of the trie.
 */
struct trie_node {
	/**
	 * A bitmap that hold which indices exist in the sparse children array.
	 * Bit i will be set if a child exists at logical index i, and its index
	 * into the array will be popcount(bitmap & ((1 << i) - 1)).
	 */
	size_t bitmap : BITMAP_WIDTH;

	/**
	 * The offset into the key in nibbles.  This is relative to the parent
	 * node, to support offsets larger than OFFSET_MAX.
	 */
	size_t offset : OFFSET_WIDTH;

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
	bfs_assert(trie_is_leaf(ptr));
	return (struct trie_leaf *)(ptr ^ 1);
}

/** Encode a pointer to a leaf. */
static uintptr_t trie_encode_leaf(const struct trie_leaf *leaf) {
	uintptr_t ptr = (uintptr_t)leaf ^ 1;
	bfs_assert(trie_is_leaf(ptr));
	return ptr;
}

/** Decode a pointer to an internal node. */
static struct trie_node *trie_decode_node(uintptr_t ptr) {
	bfs_assert(!trie_is_leaf(ptr));
	return (struct trie_node *)ptr;
}

/** Encode a pointer to an internal node. */
static uintptr_t trie_encode_node(const struct trie_node *node) {
	uintptr_t ptr = (uintptr_t)node;
	bfs_assert(!trie_is_leaf(ptr));
	return ptr;
}

void trie_init(struct trie *trie) {
	trie->root = 0;
	LIST_INIT(trie);
	VARENA_INIT(&trie->nodes, struct trie_node, children);
	VARENA_INIT(&trie->leaves, struct trie_leaf, key);
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
TARGET_CLONES_POPCNT
static struct trie_leaf *trie_representative(const struct trie *trie, const void *key, size_t length) {
	uintptr_t ptr = trie->root;
	if (!ptr) {
		return NULL;
	}

	size_t offset = 0;
	while (!trie_is_leaf(ptr)) {
		struct trie_node *node = trie_decode_node(ptr);
		offset += node->offset;

		unsigned int index = 0;
		if ((offset >> 1) < length) {
			unsigned char nibble = trie_key_nibble(key, offset);
			unsigned int bit = 1U << nibble;
			if (node->bitmap & bit) {
				index = count_ones(node->bitmap & (bit - 1));
			}
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

struct trie_leaf *trie_find_postfix(const struct trie *trie, const char *key) {
	size_t length = strlen(key);
	struct trie_leaf *rep = trie_representative(trie, key, length + 1);
	if (rep && rep->length >= length && memcmp(rep->key, key, length) == 0) {
		return rep;
	} else {
		return NULL;
	}
}

/**
 * Find a leaf that may end at the current node.
 */
static struct trie_leaf *trie_terminal_leaf(const struct trie_node *node) {
	// Finding a terminating NUL byte may take two nibbles
	for (int i = 0; i < 2; ++i) {
		if (!(node->bitmap & 1)) {
			break;
		}

		uintptr_t ptr = node->children[0];
		if (trie_is_leaf(ptr)) {
			return trie_decode_leaf(ptr);
		} else {
			node = trie_decode_node(ptr);
		}
	}

	return NULL;
}

/** Check if a leaf is a prefix of a search key. */
static bool trie_check_prefix(struct trie_leaf *leaf, size_t skip, const char *key, size_t length) {
	if (leaf && leaf->length <= length) {
		return memcmp(key + skip, leaf->key + skip, leaf->length - skip - 1) == 0;
	} else {
		return false;
	}
}

TARGET_CLONES_POPCNT
static struct trie_leaf *trie_find_prefix_impl(const struct trie *trie, const char *key) {
	uintptr_t ptr = trie->root;
	if (!ptr) {
		return NULL;
	}

	struct trie_leaf *best = NULL;
	size_t skip = 0;
	size_t length = strlen(key) + 1;

	size_t offset = 0;
	while (!trie_is_leaf(ptr)) {
		struct trie_node *node = trie_decode_node(ptr);
		offset += node->offset;
		if ((offset >> 1) >= length) {
			return best;
		}

		struct trie_leaf *leaf = trie_terminal_leaf(node);
		if (trie_check_prefix(leaf, skip, key, length)) {
			best = leaf;
			skip = offset >> 1;
		}

		unsigned char nibble = trie_key_nibble(key, offset);
		unsigned int bit = 1U << nibble;
		if (node->bitmap & bit) {
			unsigned int index = count_ones(node->bitmap & (bit - 1));
			ptr = node->children[index];
		} else {
			return best;
		}
	}

	struct trie_leaf *leaf = trie_decode_leaf(ptr);
	if (trie_check_prefix(leaf, skip, key, length)) {
		best = leaf;
	}

	return best;
}

struct trie_leaf *trie_find_prefix(const struct trie *trie, const char *key) {
	return trie_find_prefix_impl(trie, key);
}

/** Create a new leaf, holding a copy of the given key. */
static struct trie_leaf *trie_leaf_alloc(struct trie *trie, const void *key, size_t length) {
	struct trie_leaf *leaf = varena_alloc(&trie->leaves, length);
	if (!leaf) {
		return NULL;
	}

	LIST_APPEND(trie, leaf);

	leaf->value = NULL;
	leaf->length = length;
	memcpy(leaf->key, key, length);

	return leaf;
}

/** Free a leaf. */
static void trie_leaf_free(struct trie *trie, struct trie_leaf *leaf) {
	LIST_REMOVE(trie, leaf);
	varena_free(&trie->leaves, leaf, leaf->length);
}

/** Create a new node. */
static struct trie_node *trie_node_alloc(struct trie *trie, size_t size) {
	bfs_assert(has_single_bit(size));
	return varena_alloc(&trie->nodes, size);
}

/** Reallocate a trie node. */
static struct trie_node *trie_node_realloc(struct trie *trie, struct trie_node *node, size_t old_size, size_t new_size) {
	bfs_assert(has_single_bit(old_size));
	bfs_assert(has_single_bit(new_size));
	return varena_realloc(&trie->nodes, node, old_size, new_size);
}

/** Free a node. */
static void trie_node_free(struct trie *trie, struct trie_node *node, size_t size) {
	bfs_assert(size == (size_t)count_ones(node->bitmap));
	varena_free(&trie->nodes, node, size);
}

#if ENDIAN_NATIVE == ENDIAN_LITTLE
#  define TRIE_BSWAP(n) (n)
#elif ENDIAN_NATIVE == ENDIAN_BIG
#  define TRIE_BSWAP(n) bswap(n)
#endif

/** Find the offset of the first nibble that differs between two keys. */
static size_t trie_mismatch(const struct trie_leaf *rep, const void *key, size_t length) {
	if (!rep) {
		return 0;
	}

	if (rep->length < length) {
		length = rep->length;
	}

	const char *rep_bytes = rep->key;
	const char *key_bytes = key;

	size_t i = 0;
	for (size_t chunk = sizeof(chunk); i + chunk <= length; i += chunk) {
		size_t rep_chunk, key_chunk;
		memcpy(&rep_chunk, rep_bytes + i, sizeof(rep_chunk));
		memcpy(&key_chunk, key_bytes + i, sizeof(key_chunk));

		if (rep_chunk != key_chunk) {
#ifdef TRIE_BSWAP
			size_t diff = TRIE_BSWAP(rep_chunk ^ key_chunk);
			i *= 2;
			i += trailing_zeros(diff) / 4;
			return i;
#else
			break;
#endif
		}
	}

	for (; i < length; ++i) {
		unsigned char diff = rep_bytes[i] ^ key_bytes[i];
		if (diff) {
			return 2 * i + !(diff & 0xF);
		}
	}

	return 2 * i;
}

/**
 * Insert a leaf into a node.  The node must not have a child in that position
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
 *      +--->leaf
 *      | Z
 *      +--->...
 */
TARGET_CLONES_POPCNT
static struct trie_leaf *trie_node_insert(struct trie *trie, uintptr_t *ptr, struct trie_leaf *leaf, unsigned char nibble) {
	struct trie_node *node = trie_decode_node(*ptr);
	unsigned int size = count_ones(node->bitmap);

	// Double the capacity every power of two
	if (has_single_bit(size)) {
		node = trie_node_realloc(trie, node, size, 2 * size);
		if (!node) {
			trie_leaf_free(trie, leaf);
			return NULL;
		}
		*ptr = trie_encode_node(node);
	}

	unsigned int bit = 1U << nibble;

	// The child must not already be present
	bfs_assert(!(node->bitmap & bit));
	node->bitmap |= bit;

	unsigned int target = count_ones(node->bitmap & (bit - 1));
	for (size_t i = size; i > target; --i) {
		node->children[i] = node->children[i - 1];
	}
	node->children[target] = trie_encode_leaf(leaf);
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
static uintptr_t *trie_jump(struct trie *trie, uintptr_t *ptr, const char *key, size_t *offset) {
	// We only ever need to jump to leaf nodes, since internal nodes are
	// guaranteed to be within OFFSET_MAX anyway
	bfs_assert(trie_is_leaf(*ptr));

	struct trie_node *node = trie_node_alloc(trie, 1);
	if (!node) {
		return NULL;
	}

	*offset += OFFSET_MAX;
	node->offset = OFFSET_MAX;

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
 *      +--->leaf
 */
static struct trie_leaf *trie_split(struct trie *trie, uintptr_t *ptr, struct trie_leaf *leaf, struct trie_leaf *rep, size_t offset, size_t mismatch) {
	unsigned char key_nibble = trie_key_nibble(leaf->key, mismatch);
	unsigned char rep_nibble = trie_key_nibble(rep->key, mismatch);
	bfs_assert(key_nibble != rep_nibble);

	struct trie_node *node = trie_node_alloc(trie, 2);
	if (!node) {
		trie_leaf_free(trie, leaf);
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

TARGET_CLONES_POPCNT
static struct trie_leaf *trie_insert_mem_impl(struct trie *trie, const void *key, size_t length) {
	struct trie_leaf *rep = trie_representative(trie, key, length);
	size_t mismatch = trie_mismatch(rep, key, length);
	if (mismatch >= (length << 1)) {
		return rep;
	}

	struct trie_leaf *leaf = trie_leaf_alloc(trie, key, length);
	if (!leaf) {
		return NULL;
	}

	if (!rep) {
		trie->root = trie_encode_leaf(leaf);
		return leaf;
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
			bfs_assert(offset < mismatch);
			unsigned int index = count_ones(node->bitmap & (bit - 1));
			ptr = &node->children[index];
		} else {
			bfs_assert(offset == mismatch);
			return trie_node_insert(trie, ptr, leaf, nibble);
		}
	}

	while (mismatch - offset > OFFSET_MAX) {
		ptr = trie_jump(trie, ptr, key, &offset);
		if (!ptr) {
			trie_leaf_free(trie, leaf);
			return NULL;
		}
	}

	return trie_split(trie, ptr, leaf, rep, offset, mismatch);
}

struct trie_leaf *trie_insert_mem(struct trie *trie, const void *key, size_t length) {
	return trie_insert_mem_impl(trie, key, length);
}

/** Free a chain of singleton nodes. */
static void trie_free_singletons(struct trie *trie, uintptr_t ptr) {
	while (!trie_is_leaf(ptr)) {
		struct trie_node *node = trie_decode_node(ptr);

		// Make sure the bitmap is a power of two, i.e. it has just one child
		bfs_assert(has_single_bit(node->bitmap));

		ptr = node->children[0];
		trie_node_free(trie, node, 1);
	}

	trie_leaf_free(trie, trie_decode_leaf(ptr));
}

/**
 * Try to collapse a two-child node like:
 *
 *     parent child
 *       |      |
 *       v      v
 *       *----->*----->*----->leaf
 *       |
 *       +----->other
 *
 * into
 *
 *     parent
 *       |
 *       v
 *     other
 */
static int trie_collapse_node(struct trie *trie, uintptr_t *parent, struct trie_node *parent_node, unsigned int child_index) {
	uintptr_t other = parent_node->children[child_index ^ 1];
	if (!trie_is_leaf(other)) {
		struct trie_node *other_node = trie_decode_node(other);
		if (other_node->offset + parent_node->offset <= OFFSET_MAX) {
			other_node->offset += parent_node->offset;
		} else {
			return -1;
		}
	}

	*parent = other;
	trie_node_free(trie, parent_node, 1);
	return 0;
}

TARGET_CLONES_POPCNT
static void trie_remove_impl(struct trie *trie, struct trie_leaf *leaf) {
	uintptr_t *child = &trie->root;
	uintptr_t *parent = NULL;
	unsigned int child_bit = 0, child_index = 0;
	size_t offset = 0;
	while (!trie_is_leaf(*child)) {
		struct trie_node *node = trie_decode_node(*child);
		offset += node->offset;
		bfs_assert((offset >> 1) < leaf->length);

		unsigned char nibble = trie_key_nibble(leaf->key, offset);
		unsigned int bit = 1U << nibble;
		unsigned int bitmap = node->bitmap;
		bfs_assert(bitmap & bit);
		unsigned int index = count_ones(bitmap & (bit - 1));

		// Advance the parent pointer, unless this node had only one child
		if (!has_single_bit(bitmap)) {
			parent = child;
			child_bit = bit;
			child_index = index;
		}

		child = &node->children[index];
	}

	bfs_assert(trie_decode_leaf(*child) == leaf);

	if (!parent) {
		trie_free_singletons(trie, trie->root);
		trie->root = 0;
		return;
	}

	struct trie_node *node = trie_decode_node(*parent);
	child = node->children + child_index;
	trie_free_singletons(trie, *child);

	node->bitmap ^= child_bit;
	unsigned int parent_size = count_ones(node->bitmap);
	bfs_assert(parent_size > 0);
	if (parent_size == 1 && trie_collapse_node(trie, parent, node, child_index) == 0) {
		return;
	}

	if (child_index < parent_size) {
		memmove(child, child + 1, (parent_size - child_index)*sizeof(*child));
	}

	if (has_single_bit(parent_size)) {
		node = trie_node_realloc(trie, node, 2 * parent_size, parent_size);
		if (node) {
			*parent = trie_encode_node(node);
		}
	}
}

void trie_remove(struct trie *trie, struct trie_leaf *leaf) {
	trie_remove_impl(trie, leaf);
}

void trie_destroy(struct trie *trie) {
	varena_destroy(&trie->leaves);
	varena_destroy(&trie->nodes);
}
