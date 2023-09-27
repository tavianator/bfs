// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include "../src/trie.h"
#include "../src/config.h"
#include "../src/diag.h"
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

const size_t nkeys = countof(keys);

int main(void) {
	struct trie trie;
	trie_init(&trie);

	for (size_t i = 0; i < nkeys; ++i) {
		bfs_verify(!trie_find_str(&trie, keys[i]));

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
			bfs_verify(leaf);
			bfs_verify(strcmp(prefix, leaf->key) == 0);
		} else {
			bfs_verify(!leaf);
		}

		leaf = trie_insert_str(&trie, keys[i]);
		bfs_verify(leaf);
		bfs_verify(strcmp(keys[i], leaf->key) == 0);
		bfs_verify(leaf->length == strlen(keys[i]) + 1);
	}

	{
		size_t i = 0;
		for_trie (leaf, &trie) {
			bfs_verify(leaf == trie_find_str(&trie, keys[i]));
			bfs_verify(!leaf->prev || leaf->prev->next == leaf);
			bfs_verify(!leaf->next || leaf->next->prev == leaf);
			++i;
		}
		bfs_verify(i == nkeys);
	}

	for (size_t i = 0; i < nkeys; ++i) {
		struct trie_leaf *leaf = trie_find_str(&trie, keys[i]);
		bfs_verify(leaf);
		bfs_verify(strcmp(keys[i], leaf->key) == 0);
		bfs_verify(leaf->length == strlen(keys[i]) + 1);

		trie_remove(&trie, leaf);
		leaf = trie_find_str(&trie, keys[i]);
		bfs_verify(!leaf);

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
			bfs_verify(leaf);
			bfs_verify(strcmp(postfix, leaf->key) == 0);
		} else {
			bfs_verify(!leaf);
		}
	}

	for_trie (leaf, &trie) {
		bfs_verify(false);
	}

	// This tests the "jump" node handling on 32-bit platforms
	size_t longsize = 1 << 20;
	char *longstr = malloc(longsize);
	bfs_verify(longstr);

	memset(longstr, 0xAC, longsize);
	bfs_verify(!trie_find_mem(&trie, longstr, longsize));
	bfs_verify(trie_insert_mem(&trie, longstr, longsize));

	memset(longstr + longsize / 2, 0xAB, longsize / 2);
	bfs_verify(!trie_find_mem(&trie, longstr, longsize));
	bfs_verify(trie_insert_mem(&trie, longstr, longsize));

	memset(longstr, 0xAA, longsize / 2);
	bfs_verify(!trie_find_mem(&trie, longstr, longsize));
	bfs_verify(trie_insert_mem(&trie, longstr, longsize));

	free(longstr);
	trie_destroy(&trie);
	return EXIT_SUCCESS;
}
