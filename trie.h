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

#ifndef BFS_TRIE_H
#define BFS_TRIE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * A trie that holds a set of fixed- or variable-length strings.
 */
struct trie {
	uintptr_t root;
};

/**
 * A leaf of a trie.
 */
struct trie_leaf {
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
 * Initialize an empty trie.
 */
void trie_init(struct trie *trie);

/**
 * Get the first (lexicographically earliest) leaf in the trie.
 *
 * @param trie
 *         The trie to search.
 * @return
 *         The first leaf, or NULL if the trie is empty.
 */
struct trie_leaf *trie_first_leaf(const struct trie *trie);

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

#endif // BFS_TRIE_H
