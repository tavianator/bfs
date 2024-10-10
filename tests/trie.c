// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include "tests.h"

#include "bfs.h"
#include "diag.h"
#include "trie.h"

#include <stdlib.h>
#include <string.h>

static const char *keys[] = {
	"foo",
	"bar",
	"baz",
	"qux",
	"quux",
	"quuux",
	"quuuux",

	"pre",
	"prefi",
	"pref",
	"prefix",
	"p",
	"pRefix",

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

static const size_t nkeys = countof(keys);

void check_trie(void) {
	struct trie trie;
	trie_init(&trie);

	for (size_t i = 0; i < nkeys; ++i) {
		bfs_check(!trie_find_str(&trie, keys[i]));

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
			bfs_check(strcmp(prefix, leaf->key) == 0);
		} else {
			bfs_check(!leaf);
		}

		leaf = trie_insert_str(&trie, keys[i]);
		bfs_verify(leaf);
		bfs_check(strcmp(keys[i], leaf->key) == 0);
		bfs_check(leaf->length == strlen(keys[i]) + 1);
	}

	{
		size_t i = 0;
		for_trie (leaf, &trie) {
			bfs_check(leaf == trie_find_str(&trie, keys[i]));
			bfs_check(leaf == trie_insert_str(&trie, keys[i]));
			bfs_check(!leaf->prev || leaf->prev->next == leaf);
			bfs_check(!leaf->next || leaf->next->prev == leaf);
			++i;
		}
		bfs_check(i == nkeys);
	}

	for (size_t i = 0; i < nkeys; ++i) {
		struct trie_leaf *leaf = trie_find_str(&trie, keys[i]);
		bfs_verify(leaf);
		bfs_check(strcmp(keys[i], leaf->key) == 0);
		bfs_check(leaf->length == strlen(keys[i]) + 1);

		trie_remove(&trie, leaf);
		leaf = trie_find_str(&trie, keys[i]);
		bfs_check(!leaf);

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
			bfs_check(strcmp(postfix, leaf->key) == 0);
		} else {
			bfs_check(!leaf);
		}
	}

	for_trie (leaf, &trie) {
		bfs_check(false, "trie should be empty");
	}

	// This tests the "jump" node handling on 32-bit platforms
	size_t longsize = 1 << 20;
	char *longstr = malloc(longsize);
	bfs_verify(longstr);

	memset(longstr, 0xAC, longsize);
	bfs_check(!trie_find_mem(&trie, longstr, longsize));
	bfs_check(trie_insert_mem(&trie, longstr, longsize));

	memset(longstr + longsize / 2, 0xAB, longsize / 2);
	bfs_check(!trie_find_mem(&trie, longstr, longsize));
	bfs_check(trie_insert_mem(&trie, longstr, longsize));

	memset(longstr, 0xAA, longsize / 2);
	bfs_check(!trie_find_mem(&trie, longstr, longsize));
	bfs_check(trie_insert_mem(&trie, longstr, longsize));

	free(longstr);
	trie_destroy(&trie);
}
