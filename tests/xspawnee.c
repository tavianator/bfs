// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include "bfstd.h"
#include "diag.h"

#include <stdlib.h>
#include <string.h>

/** Child binary for bfs_spawn() tests. */
int main(int argc, char *argv[]) {
	// Check that bfs_spawn_addopen() didn't leak a file descriptor
	bfs_verify(!isopen(3));

	if (argc >= 2) {
		const char *path = getenv("PATH");
		bfs_verify(path && strcmp(path, argv[1]) == 0);
	}

	return EXIT_SUCCESS;
}
