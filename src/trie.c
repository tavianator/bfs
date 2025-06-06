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
#include "bfs.h"
#include "bit.h"
#include "diag.h"
#include "list.h"

#include <stdint.h>
#include <string.h>

static_assert(CHAR_WIDTH == 8, "This trie implementation assumes 8-bit bytes.");

#if __i386__ || __x86_64__
#  define _trie_clones _target_clones("popcnt", "default")
#else
#  define _trie_clones
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
	uintptr_t children[]; // _counted_by(count_ones(bitmap))
};

/** Check if an encoded pointer is to an internal node. */
static bool trie_is_node(uintptr_t ptr) {
	return ptr & 1;
}

/** Decode a pointer to an internal node. */
static struct trie_node *trie_decode_node(uintptr_t ptr) {
	bfs_assert(trie_is_node(ptr));
	return (struct trie_node *)(ptr - 1);
}

/** Encode a pointer to an internal node. */
static uintptr_t trie_encode_node(const struct trie_node *node) {
	uintptr_t ptr = (uintptr_t)node + 1;
	bfs_assert(trie_is_node(ptr));
	return ptr;
}

/** Decode a pointer to a leaf. */
static struct trie_leaf *trie_decode_leaf(uintptr_t ptr) {
	bfs_assert(!trie_is_node(ptr));
	return (struct trie_leaf *)ptr;
}

/** Encode a pointer to a leaf. */
static uintptr_t trie_encode_leaf(const struct trie_leaf *leaf) {
	uintptr_t ptr = (uintptr_t)leaf;
	bfs_assert(!trie_is_node(ptr));
	return ptr;
}

void trie_init(struct trie *trie) {
	trie->root = 0;
	LIST_INIT(trie);
	VARENA_INIT(&trie->nodes, struct trie_node, children);
	VARENA_INIT(&trie->leaves, struct trie_leaf, key);
}

/** Extract the nibble at a certain offset from a byte sequence. */
static unsigned char trie_key_nibble(const void *key, size_t length, size_t offset) {
	const unsigned char *bytes = key;
	size_t byte = offset / 2;
	bfs_assert(byte < length);

	// A branchless version of
	// if (offset & 1) {
	//         return bytes[byte] & 0xF;
	// } else {
	//         return bytes[byte] >> 4;
	// }
	unsigned int shift = 4 * ((offset + 1) % 2);
	return (bytes[byte] >> shift) & 0xF;
}

/** Extract the nibble at a certain offset from a leaf. */
static unsigned char trie_leaf_nibble(const struct trie_leaf *leaf, size_t offset) {
	return trie_key_nibble(leaf->key, leaf->length, offset);
}

/** Get the number of children of an internal node. */
_trie_clones
static unsigned int trie_node_size(const struct trie_node *node) {
	return count_ones((unsigned int)node->bitmap);
}

/**
 * Finds a leaf in the trie that matches the key at every branch.  If the key
 * exists in the trie, the representative will match the searched key.  But
 * since only branch points are tested, it can be different from the key.  In
 * that case, the first mismatch between the key and the representative will be
 * the depth at which to make a new branch to insert the key.
 */
_trie_clones
static struct trie_leaf *trie_representative(const struct trie *trie, const void *key, size_t length) {
	uintptr_t ptr = trie->root;

	size_t offset = 0, limit = 2 * length;
	while (trie_is_node(ptr)) {
		struct trie_node *node = trie_decode_node(ptr);
		offset += node->offset;

		unsigned int index = 0;
		if (offset < limit) {
			unsigned char nibble = trie_key_nibble(key, length, offset);
			unsigned int bit = 1U << nibble;
			unsigned int map = node->bitmap;
			unsigned int bits = map & (bit - 1);
			unsigned int mask = -!!(map & bit);
			// index = (map & bit) ? count_ones(bits) : 0;
			index = count_ones(bits) & mask;
		}
		ptr = node->children[index];
	}

	return trie_decode_leaf(ptr);
}

struct trie_leaf *trie_find_str(const struct trie *trie, const char *key) {
	return trie_find_mem(trie, key, strlen(key) + 1);
}

_trie_clones
static struct trie_leaf *trie_find_mem_impl(const struct trie *trie, const void *key, size_t length) {
	struct trie_leaf *rep = trie_representative(trie, key, length);
	if (rep && rep->length == length && memcmp(rep->key, key, length) == 0) {
		return rep;
	} else {
		return NULL;
	}
}

struct trie_leaf *trie_find_mem(const struct trie *trie, const void *key, size_t length) {
	return trie_find_mem_impl(trie, key, length);
}

void *trie_get_str(const struct trie *trie, const char *key) {
	const struct trie_leaf *leaf = trie_find_str(trie, key);
	return leaf ? leaf->value : NULL;
}

void *trie_get_mem(const struct trie *trie, const void *key, size_t length) {
	const struct trie_leaf *leaf = trie_find_mem(trie, key, length);
	return leaf ? leaf->value : NULL;
}

_trie_clones
static struct trie_leaf *trie_find_postfix_impl(const struct trie *trie, const char *key) {
	size_t length = strlen(key);
	struct trie_leaf *rep = trie_representative(trie, key, length + 1);
	if (rep && rep->length >= length && memcmp(rep->key, key, length) == 0) {
		return rep;
	} else {
		return NULL;
	}
}

struct trie_leaf *trie_find_postfix(const struct trie *trie, const char *key) {
	return trie_find_postfix_impl(trie, key);
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
		if (trie_is_node(ptr)) {
			node = trie_decode_node(ptr);
		} else {
			return trie_decode_leaf(ptr);
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

_trie_clones
static struct trie_leaf *trie_find_prefix_impl(const struct trie *trie, const char *key) {
	uintptr_t ptr = trie->root;
	if (!ptr) {
		return NULL;
	}

	struct trie_leaf *best = NULL;
	size_t skip = 0;
	size_t length = strlen(key) + 1;

	size_t offset = 0, limit = 2 * length;
	while (trie_is_node(ptr)) {
		struct trie_node *node = trie_decode_node(ptr);
		offset += node->offset;
		if (offset >= limit) {
			return best;
		}

		struct trie_leaf *leaf = trie_terminal_leaf(node);
		if (trie_check_prefix(leaf, skip, key, length)) {
			best = leaf;
			skip = offset / 2;
		}

		unsigned char nibble = trie_key_nibble(key, length, offset);
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

	LIST_ITEM_INIT(leaf);
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
	bfs_assert(size == trie_node_size(node));
	varena_free(&trie->nodes, node, size);
}

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

	size_t ret = 0, i = 0;

#define CHUNK(n) CHUNK_(uint##n##_t, load8_beu##n)
#define CHUNK_(type, load8) \
	(length - i >= sizeof(type)) { \
		type rep_chunk = load8(rep_bytes + i); \
		type key_chunk = load8(key_bytes + i); \
		type diff = rep_chunk ^ key_chunk; \
		ret += leading_zeros(diff) / 4; \
		if (diff) { \
			return ret; \
		} \
		i += sizeof(type); \
	}

#if SIZE_WIDTH >= 64
	while CHUNK(64);
	if CHUNK(32);
#else
	while CHUNK(32);
#endif
	if CHUNK(16);
	if CHUNK(8);

#undef CHUNK_
#undef CHUNK

	return ret;
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
_trie_clones
static struct trie_leaf *trie_node_insert(struct trie *trie, uintptr_t *ptr, struct trie_leaf *leaf, unsigned char nibble) {
	struct trie_node *node = trie_decode_node(*ptr);
	unsigned int size = trie_node_size(node);

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
static uintptr_t *trie_jump(struct trie *trie, uintptr_t *ptr, size_t *offset) {
	// We only ever need to jump to leaf nodes, since internal nodes are
	// guaranteed to be within OFFSET_MAX anyway
	struct trie_leaf *leaf = trie_decode_leaf(*ptr);

	struct trie_node *node = trie_node_alloc(trie, 1);
	if (!node) {
		return NULL;
	}

	*offset += OFFSET_MAX;
	node->offset = OFFSET_MAX;

	unsigned char nibble = trie_leaf_nibble(leaf, *offset);
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
	unsigned char key_nibble = trie_leaf_nibble(leaf, mismatch);
	unsigned char rep_nibble = trie_leaf_nibble(rep, mismatch);
	bfs_assert(key_nibble != rep_nibble);

	struct trie_node *node = trie_node_alloc(trie, 2);
	if (!node) {
		trie_leaf_free(trie, leaf);
		return NULL;
	}

	node->bitmap = (1 << key_nibble) | (1 << rep_nibble);

	size_t delta = mismatch - offset;
	if (trie_is_node(*ptr)) {
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

_trie_clones
static struct trie_leaf *trie_insert_mem_impl(struct trie *trie, const void *key, size_t length) {
	struct trie_leaf *rep = trie_representative(trie, key, length);
	size_t mismatch = trie_mismatch(rep, key, length);
	size_t misbyte = mismatch / 2;
	if (misbyte >= length) {
		bfs_assert(misbyte == length);
		return rep;
	} else if (rep && misbyte >= rep->length) {
		bfs_bug("trie keys must be prefix-free");
		errno = EINVAL;
		return NULL;
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
	while (trie_is_node(*ptr)) {
		struct trie_node *node = trie_decode_node(*ptr);
		if (offset + node->offset > mismatch) {
			break;
		}
		offset += node->offset;

		unsigned char nibble = trie_leaf_nibble(leaf, offset);
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
		ptr = trie_jump(trie, ptr, &offset);
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

int trie_set_str(struct trie *trie, const char *key, const void *value) {
	struct trie_leaf *leaf = trie_insert_str(trie, key);
	if (leaf) {
		leaf->value = (void *)value;
		return 0;
	} else {
		return -1;
	}
}

int trie_set_mem(struct trie *trie, const void *key, size_t length, const void *value) {
	struct trie_leaf *leaf = trie_insert_mem(trie, key, length);
	if (leaf) {
		leaf->value = (void *)value;
		return 0;
	} else {
		return -1;
	}
}

/** Free a chain of singleton nodes. */
static void trie_free_singletons(struct trie *trie, uintptr_t ptr) {
	while (trie_is_node(ptr)) {
		struct trie_node *node = trie_decode_node(ptr);

		// Make sure the bitmap is a power of two, i.e. it has just one child
		bfs_assert(has_single_bit((size_t)node->bitmap));

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
	if (trie_is_node(other)) {
		struct trie_node *other_node = trie_decode_node(other);
		if (other_node->offset + parent_node->offset <= OFFSET_MAX) {
			other_node->offset += parent_node->offset;
		} else {
			return -1;
		}
	}

	*parent = other;
	trie_node_free(trie, parent_node, 2);
	return 0;
}

_trie_clones
static void trie_remove_impl(struct trie *trie, struct trie_leaf *leaf) {
	uintptr_t *child = &trie->root;
	uintptr_t *parent = NULL;
	unsigned int child_bit = 0, child_index = 0;
	size_t offset = 0;
	while (trie_is_node(*child)) {
		struct trie_node *node = trie_decode_node(*child);
		offset += node->offset;

		unsigned char nibble = trie_leaf_nibble(leaf, offset);
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
	trie_free_singletons(trie, node->children[child_index]);

	unsigned int parent_size = trie_node_size(node);
	bfs_assert(parent_size > 1);
	if (parent_size == 2 && trie_collapse_node(trie, parent, node, child_index) == 0) {
		return;
	}

	for (size_t i = child_index; i + 1 < parent_size; ++i) {
		node->children[i] = node->children[i + 1];
	}
	node->bitmap &= ~child_bit;
	--parent_size;

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

void trie_clear(struct trie *trie) {
	trie->root = 0;
	LIST_INIT(trie);

	varena_clear(&trie->leaves);
	varena_clear(&trie->nodes);
}

void trie_destroy(struct trie *trie) {
	varena_destroy(&trie->leaves);
	varena_destroy(&trie->nodes);
}
