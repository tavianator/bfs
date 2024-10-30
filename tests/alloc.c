// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include "tests.h"

#include "alloc.h"
#include "diag.h"

#include <errno.h>
#include <stdlib.h>
#include <stdint.h>

struct flexible {
	alignas(64) int foo[8];
	int bar[];
};

/** Check varena_realloc() poisoning for a size combination. */
static struct flexible *check_varena_realloc(struct varena *varena, struct flexible *flexy, size_t old_count, size_t new_count) {
	flexy = varena_realloc(varena, flexy, old_count, new_count);
	bfs_everify(flexy);

	for (size_t i = 0; i < new_count; ++i) {
		if (i < old_count) {
			bfs_check(flexy->bar[i] == (int)i);
		} else {
			flexy->bar[i] = i;
		}
	}

	return flexy;
}

void check_alloc(void) {
	// Check aligned allocation
	void *ptr;
	bfs_everify((ptr = zalloc(64, 129)));
	bfs_check((uintptr_t)ptr % 64 == 0);
	bfs_echeck((ptr = xrealloc(ptr, 64, 129, 65)));
	bfs_check((uintptr_t)ptr % 64 == 0);
	free(ptr);

	// Check sizeof_flex()
	bfs_check(sizeof_flex(struct flexible, bar, 0) >= sizeof(struct flexible));
	bfs_check(sizeof_flex(struct flexible, bar, 16) % alignof(struct flexible) == 0);

	// volatile to suppress -Walloc-size-larger-than
	volatile size_t too_many = SIZE_MAX / sizeof(int) + 1;
	bfs_check(sizeof_flex(struct flexible, bar, too_many) == align_floor(alignof(struct flexible), SIZE_MAX));

	// Make sure we detect allocation size overflows
	bfs_check(ALLOC_ARRAY(int, too_many) == NULL && errno == EOVERFLOW);
	bfs_check(ZALLOC_ARRAY(int, too_many) == NULL && errno == EOVERFLOW);
	bfs_check(ALLOC_FLEX(struct flexible, bar, too_many) == NULL && errno == EOVERFLOW);
	bfs_check(ZALLOC_FLEX(struct flexible, bar, too_many) == NULL && errno == EOVERFLOW);

	// varena tests
	struct varena varena;
	VARENA_INIT(&varena, struct flexible, bar);

	for (size_t i = 0; i < 256; ++i) {
		bfs_everify(varena_alloc(&varena, i));
		struct arena *arena = &varena.arenas[varena.narenas - 1];
		bfs_check(arena->size >= sizeof_flex(struct flexible, bar, i));
	}

	// Check varena_realloc() (un)poisoning
	struct flexible *flexy = varena_alloc(&varena, 160);
	bfs_everify(flexy);

	flexy = check_varena_realloc(&varena, flexy, 0, 160);
	flexy = check_varena_realloc(&varena, flexy, 160, 192);
	flexy = check_varena_realloc(&varena, flexy, 192, 160);
	flexy = check_varena_realloc(&varena, flexy, 160, 320);
	flexy = check_varena_realloc(&varena, flexy, 320, 96);

	varena_destroy(&varena);
}
