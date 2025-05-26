// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#ifndef BFS_TRIE_H
#define BFS_TRIE_H

#include "alloc.h"
#include "list.h"

#include <stddef.h>
#include <stdint.h>

/**
 * A leaf of a trie.
 */
struct trie_leaf {
	/** Linked list of leaves, in insertion order. */
	struct trie_leaf *prev, *next;
	/** An arbitrary value associated with this leaf. */
	void *value;
	/** The length of the key in bytes. */
	size_t length;
	/** The key itself, stored inline. */
	char key[] _counted_by(length);
};

/**
 * A trie that holds a set of fixed- or variable-length strings.
 */
struct trie {
	/** Pointer to the root node/leaf. */
	uintptr_t root;
	/** Linked list of leaves. */
	struct trie_leaf *head, *tail;
	/** Node allocator. */
	struct varena nodes;
	/** Leaf allocator. */
	struct varena leaves;
};

/**
 * Initialize an empty trie.
 */
void trie_init(struct trie *trie);

/**
 * Find the leaf for a string key.
 *
 * @trie
 *         The trie to search.
 * @key
 *         The key to look up.
 * @return
 *         The found leaf, or NULL if the key is not present.
 */
struct trie_leaf *trie_find_str(const struct trie *trie, const char *key);

/**
 * Find the leaf for a fixed-size key.
 *
 * @trie
 *         The trie to search.
 * @key
 *         The key to look up.
 * @length
 *         The length of the key in bytes.
 * @return
 *         The found leaf, or NULL if the key is not present.
 */
struct trie_leaf *trie_find_mem(const struct trie *trie, const void *key, size_t length);

/**
 * Get the value associated with a string key.
 *
 * @trie
 *         The trie to search.
 * @key
 *         The key to look up.
 * @return
 *         The found value, or NULL if the key is not present.
 */
void *trie_get_str(const struct trie *trie, const char *key);

/**
 * Get the value associated with a fixed-size key.
 *
 * @trie
 *         The trie to search.
 * @key
 *         The key to look up.
 * @length
 *         The length of the key in bytes.
 * @return
 *         The found value, or NULL if the key is not present.
 */
void *trie_get_mem(const struct trie *trie, const void *key, size_t length);

/**
 * Find the shortest leaf that starts with a given key.
 *
 * @trie
 *         The trie to search.
 * @key
 *         The key to look up.
 * @return
 *         A leaf that starts with the given key, or NULL.
 */
struct trie_leaf *trie_find_postfix(const struct trie *trie, const char *key);

/**
 * Find the leaf that is the longest prefix of the given key.
 *
 * @trie
 *         The trie to search.
 * @key
 *         The key to look up.
 * @return
 *         The longest prefix match for the given key, or NULL.
 */
struct trie_leaf *trie_find_prefix(const struct trie *trie, const char *key);

/**
 * Insert a string key into the trie.
 *
 * @trie
 *         The trie to modify.
 * @key
 *         The key to insert.
 * @return
 *         The inserted leaf, or NULL on failure.
 */
struct trie_leaf *trie_insert_str(struct trie *trie, const char *key);

/**
 * Insert a fixed-size key into the trie.
 *
 * @trie
 *         The trie to modify.
 * @key
 *         The key to insert.
 * @length
 *         The length of the key in bytes.
 * @return
 *         The inserted leaf, or NULL on failure.
 */
struct trie_leaf *trie_insert_mem(struct trie *trie, const void *key, size_t length);

/**
 * Set the value for a string key.
 *
 * @trie
 *         The trie to modify.
 * @key
 *         The key to insert.
 * @value
 *         The value to set.
 * @return
 *         0 on success, -1 on error.
 */
int trie_set_str(struct trie *trie, const char *key, const void *value);

/**
 * Set the value for a fixed-size key.
 *
 * @trie
 *         The trie to modify.
 * @key
 *         The key to insert.
 * @length
 *         The length of the key in bytes.
 * @value
 *         The value to set.
 * @return
 *         0 on success, -1 on error.
 */
int trie_set_mem(struct trie *trie, const void *key, size_t length, const void *value);

/**
 * Remove a leaf from a trie.
 *
 * @trie
 *         The trie to modify.
 * @leaf
 *         The leaf to remove.
 */
void trie_remove(struct trie *trie, struct trie_leaf *leaf);

/**
 * Remove all leaves from a trie.
 */
void trie_clear(struct trie *trie);

/**
 * Destroy a trie and its contents.
 */
void trie_destroy(struct trie *trie);

/**
 * Iterate over the leaves of a trie.
 */
#define for_trie(leaf, trie) \
	for_list (struct trie_leaf, leaf, trie)

#endif // BFS_TRIE_H
