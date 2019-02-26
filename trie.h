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
 * A trie that holds a set of fixed- or dynamic-length strings.
 */
struct trie {
	uintptr_t root;
};

/**
 * Initialize an empty trie.
 */
void trie_init(struct trie *trie);

/**
 * Check if a NUL-terminated string key exists in a trie.
 *
 * @param trie
 *         The trie to search.
 * @param key
 *         The key to search for.
 * @return
 *         Whether the key was found.
 */
bool trie_strsearch(const struct trie *trie, const char *key);

/**
 * Check if a prefix of a NUL-terminated string key exists in a trie.
 *
 * @param trie
 *         The trie to search.
 * @param key
 *         The key to search for.
 * @param size
 *         The length of the prefix to consider, in bytes.
 * @return
 *         Whether the key was found.
 */
bool trie_strnsearch(const struct trie *trie, const char *key, size_t size);

/**
 * Check if a fixed-length key exists in a trie.
 *
 * @param trie
 *         The trie to search.
 * @param key
 *         The key to search for.
 * @param size
 *         The length of the key in bytes.
 * @return
 *         Whether the key was found.
 */
bool trie_memsearch(const struct trie *trie, const void *key, size_t size);

/**
 * Insert a NUL-terminated string key in the trie.
 *
 * @param trie
 *         The trie to modify.
 * @param key
 *         The key to insert.
 * @return
 *         0 on success, 1 if the key was already present, or -1 on error.
 */
int trie_strinsert(struct trie *trie, const char *key);

/**
 * Insert a fixed-length key in the trie.
 *
 * @param trie
 *         The trie to modify.
 * @param key
 *         The key to insert.
 * @param size
 *         The length of the key in bytes.
 * @return
 *         0 on success, 1 if the key was already present, or -1 on error.
 */
int trie_meminsert(struct trie *trie, const void *key, size_t size);

/**
 * Destroy a trie and its contents.
 */
void trie_destroy(struct trie *trie);

#endif // BFS_TRIE_H
