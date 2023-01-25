// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#ifndef BFS_TRIE_H
#define BFS_TRIE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * A leaf of a trie.
 */
struct trie_leaf {
	/**
	 * Linked list of leaves, in insertion order.
	 */
	struct trie_leaf *prev, *next;

	/**
	 * An arbitrary value associated with this leaf.
	 */
	void *value;

	/**
	 * The length of the key in bytes.
	 */
	size_t length;

	/**
	 * The key itself, stored inline.
	 */
	char key[];
};

/**
 * A trie that holds a set of fixed- or variable-length strings.
 */
struct trie {
	/** Pointer to the root node/leaf. */
	uintptr_t root;
	/** Linked list of leaves. */
	struct trie_leaf *head, *tail;
};

/**
 * Initialize an empty trie.
 */
void trie_init(struct trie *trie);

/**
 * Find the leaf for a string key.
 *
 * @param trie
 *         The trie to search.
 * @param key
 *         The key to look up.
 * @return
 *         The found leaf, or NULL if the key is not present.
 */
struct trie_leaf *trie_find_str(const struct trie *trie, const char *key);

/**
 * Find the leaf for a fixed-size key.
 *
 * @param trie
 *         The trie to search.
 * @param key
 *         The key to look up.
 * @param length
 *         The length of the key in bytes.
 * @return
 *         The found leaf, or NULL if the key is not present.
 */
struct trie_leaf *trie_find_mem(const struct trie *trie, const void *key, size_t length);

/**
 * Find the shortest leaf that starts with a given key.
 *
 * @param trie
 *         The trie to search.
 * @param key
 *         The key to look up.
 * @return
 *         A leaf that starts with the given key, or NULL.
 */
struct trie_leaf *trie_find_postfix(const struct trie *trie, const char *key);

/**
 * Find the leaf that is the longest prefix of the given key.
 *
 * @param trie
 *         The trie to search.
 * @param key
 *         The key to look up.
 * @return
 *         The longest prefix match for the given key, or NULL.
 */
struct trie_leaf *trie_find_prefix(const struct trie *trie, const char *key);

/**
 * Insert a string key into the trie.
 *
 * @param trie
 *         The trie to modify.
 * @param key
 *         The key to insert.
 * @return
 *         The inserted leaf, or NULL on failure.
 */
struct trie_leaf *trie_insert_str(struct trie *trie, const char *key);

/**
 * Insert a fixed-size key into the trie.
 *
 * @param trie
 *         The trie to modify.
 * @param key
 *         The key to insert.
 * @param length
 *         The length of the key in bytes.
 * @return
 *         The inserted leaf, or NULL on failure.
 */
struct trie_leaf *trie_insert_mem(struct trie *trie, const void *key, size_t length);

/**
 * Remove a leaf from a trie.
 *
 * @param trie
 *         The trie to modify.
 * @param leaf
 *         The leaf to remove.
 */
void trie_remove(struct trie *trie, struct trie_leaf *leaf);

/**
 * Destroy a trie and its contents.
 */
void trie_destroy(struct trie *trie);

/**
 * Iterate over the leaves of a trie.
 */
#define TRIE_FOR_EACH(trie, leaf)				\
	for (struct trie_leaf *leaf = (trie)->head, *_next;	\
	     leaf && (_next = leaf->next, true);		\
	     leaf = _next)

#endif // BFS_TRIE_H
