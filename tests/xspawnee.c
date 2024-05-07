// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include <stdlib.h>
#include <string.h>

/** Child binary for bfs_spawn() tests. */
int main(int argc, char *argv[]) {
	if (argc >= 2) {
		const char *path = getenv("PATH");
		if (!path || strcmp(path, argv[1]) != 0) {
			return EXIT_FAILURE;
		}
	}

	return EXIT_SUCCESS;
}
