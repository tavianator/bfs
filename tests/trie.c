/****************************************************************************
 * bfs                                                                      *
 * Copyright (C) 2020-2022 Tavian Barnes <tavianator@tavianator.com>        *
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

#undef NDEBUG

#include "../trie.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

const char *keys[] = {
	"foo",
	"bar",
	"baz",
	"qux",
	"quux",
	"quuux",
	"quuuux",

	"pre",
	"pref",
	"prefi",
	"prefix",

	"AAAA",
	"AADD",
	"ABCD",
	"DDAA",
	"DDDD",

	"<<<",
	"<<<>>>",
	"<<<<<<",
	"<<<<<<>>>>>>",
	">>>>>>",
	">>><<<",
	">>>",
};

const size_t nkeys = sizeof(keys) / sizeof(keys[0]);

int main(void) {
	struct trie trie;
	trie_init(&trie);

	for (size_t i = 0; i < nkeys; ++i) {
		assert(!trie_find_str(&trie, keys[i]));

		const char *prefix = NULL;
		for (size_t j = 0; j < i; ++j) {
			if (strncmp(keys[i], keys[j], strlen(keys[j])) == 0) {
				if (!prefix || strcmp(keys[j], prefix) > 0) {
					prefix = keys[j];
				}
			}
		}

		struct trie_leaf *leaf = trie_find_prefix(&trie, keys[i]);
		if (prefix) {
			assert(leaf);
			assert(strcmp(prefix, leaf->key) == 0);
		} else {
			assert(!leaf);
		}

		leaf = trie_insert_str(&trie, keys[i]);
		assert(leaf);
		assert(strcmp(keys[i], leaf->key) == 0);
		assert(leaf->length == strlen(keys[i]) + 1);
	}

	for (size_t i = 0; i < nkeys; ++i) {
		struct trie_leaf *leaf = trie_find_str(&trie, keys[i]);
		assert(leaf);
		assert(strcmp(keys[i], leaf->key) == 0);
		assert(leaf->length == strlen(keys[i]) + 1);

		trie_remove(&trie, leaf);
		leaf = trie_find_str(&trie, keys[i]);
		assert(!leaf);

		const char *postfix = NULL;
		for (size_t j = i + 1; j < nkeys; ++j) {
			if (strncmp(keys[i], keys[j], strlen(keys[i])) == 0) {
				if (!postfix || strcmp(keys[j], postfix) < 0) {
					postfix = keys[j];
				}
			}
		}

		leaf = trie_find_postfix(&trie, keys[i]);
		if (postfix) {
			assert(leaf);
			assert(strcmp(postfix, leaf->key) == 0);
		} else {
			assert(!leaf);
		}
	}

	// This tests the "jump" node handling on 32-bit platforms
	size_t longsize = 1 << 20;
	char *longstr = malloc(longsize);
	assert(longstr);

	memset(longstr, 0xAC, longsize);
	assert(!trie_find_mem(&trie, longstr, longsize));
	assert(trie_insert_mem(&trie, longstr, longsize));

	memset(longstr + longsize/2, 0xAB, longsize/2);
	assert(!trie_find_mem(&trie, longstr, longsize));
	assert(trie_insert_mem(&trie, longstr, longsize));

	memset(longstr, 0xAA, longsize/2);
	assert(!trie_find_mem(&trie, longstr, longsize));
	assert(trie_insert_mem(&trie, longstr, longsize));

	free(longstr);
	trie_destroy(&trie);
	return EXIT_SUCCESS;
}
