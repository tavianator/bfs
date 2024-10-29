// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include "tests.h"

#include "alloc.h"
#include "diag.h"

#include <errno.h>
#include <stdlib.h>
#include <stdint.h>

void check_alloc(void) {
	// Check aligned allocation
	void *ptr;
	bfs_everify((ptr = zalloc(64, 129)));
	bfs_check((uintptr_t)ptr % 64 == 0);
	bfs_echeck((ptr = xrealloc(ptr, 64, 129, 65)));
	bfs_check((uintptr_t)ptr % 64 == 0);
	free(ptr);

	// Check sizeof_flex()
	struct flexible {
		alignas(64) int foo[8];
		int bar[];
	};
	bfs_check(sizeof_flex(struct flexible, bar, 0) >= sizeof(struct flexible));
	bfs_check(sizeof_flex(struct flexible, bar, 16) % alignof(struct flexible) == 0);

	size_t too_many = SIZE_MAX / sizeof(int) + 1;
	bfs_check(sizeof_flex(struct flexible, bar, too_many) == align_floor(alignof(struct flexible), SIZE_MAX));

	// Corner case: sizeof(type) > align_ceil(alignof(type), offsetof(type, member))
	// Doesn't happen in typical ABIs
	bfs_check(flex_size(8, 16, 4, 4, 1) == 16);

	// Make sure we detect allocation size overflows
#if __GNUC__ && !__clang__
#  pragma GCC diagnostic ignored "-Walloc-size-larger-than="
#endif

	bfs_check(ALLOC_ARRAY(int, too_many) == NULL && errno == EOVERFLOW);
	bfs_check(ZALLOC_ARRAY(int, too_many) == NULL && errno == EOVERFLOW);
	bfs_check(ALLOC_FLEX(struct flexible, bar, too_many) == NULL && errno == EOVERFLOW);
	bfs_check(ZALLOC_FLEX(struct flexible, bar, too_many) == NULL && errno == EOVERFLOW);

	// varena tests
	struct varena varena;
	VARENA_INIT(&varena, struct flexible, bar);

	for (size_t i = 0; i < 256; ++i) {
		bfs_verify(varena_alloc(&varena, i));
		struct arena *arena = &varena.arenas[varena.narenas - 1];
		bfs_check(arena->size >= sizeof_flex(struct flexible, bar, i));
	}

	varena_destroy(&varena);
}
