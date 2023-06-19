// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include "../src/alloc.h"
#include "../src/diag.h"
#include <stdlib.h>

int main(void) {
	// Check sizeof_flex()
	struct flexible {
		alignas(64) int foo[8];
		int bar[];
	};
	bfs_verify(sizeof_flex(struct flexible, bar, 0) >= sizeof(struct flexible));
	bfs_verify(sizeof_flex(struct flexible, bar, 16) % alignof(struct flexible) == 0);
	bfs_verify(sizeof_flex(struct flexible, bar, SIZE_MAX / sizeof(int) + 1)
	           == align_floor(alignof(struct flexible), SIZE_MAX));

	// Corner case: sizeof(type) > align_ceil(alignof(type), offsetof(type, member))
	// Doesn't happen in typical ABIs
	bfs_verify(flex_size(8, 16, 4, 4, 1) == 16);

	// varena tests
	struct varena varena;
	VARENA_INIT(&varena, struct flexible, bar);

	for (size_t i = 0; i < 256; ++i) {
		bfs_verify(varena_alloc(&varena, i));
		struct arena *arena = &varena.arenas[varena.narenas - 1];
		bfs_verify(arena->size >= sizeof_flex(struct flexible, bar, i));
	}

	varena_destroy(&varena);

	return EXIT_SUCCESS;
}
