// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

/**
 * There's no standard Unix utility that creates a socket file, so this small
 * program does the job.
 */

#include "../src/bfstd.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

/**
 * Print an error message.
 */
static void errmsg(const char *cmd, const char *path) {
	fprintf(stderr, "%s: '%s': %s.\n", cmd, path, strerror(errno));
}

/**
 * struct sockaddr_un::sun_path is very short, so we chdir() into the target
 * directory before creating sockets in case the full path is too long but the
 * file name is not.
 */
static int chdir_parent(const char *path) {
	char *dir = xdirname(path);
	if (!dir) {
		return -1;
	}

	int ret = chdir(dir);
	free(dir);
	return ret;
}

/**
 * Initialize a struct sockaddr_un with the right filename.
 */
static int init_sun(struct sockaddr_un *sock, const char *path) {
	size_t len = strlen(path);
	if (len == 0 || path[len - 1] == '/') {
		errno = ENOENT;
		return -1;
	}

	char *base = xbasename(path);
	if (!base) {
		return -1;
	}

	len = strlen(base);
	if (len >= sizeof(sock->sun_path)) {
		free(base);
		errno = ENAMETOOLONG;
		return -1;
	}

	sock->sun_family = AF_UNIX;
	memcpy(sock->sun_path, base, len + 1);
	free(base);
	return 0;
}

int main(int argc, char *argv[]) {
	const char *cmd = argc > 0 ? argv[0] : "mksock";

	if (argc != 2) {
		fprintf(stderr, "Usage: %s NAME\n", cmd);
		return EXIT_FAILURE;
	}

	const char *path = argv[1];

	if (chdir_parent(path) != 0) {
		errmsg(cmd, path);
		return EXIT_FAILURE;
	}

	struct sockaddr_un sock;
	if (init_sun(&sock, path) != 0) {
		errmsg(cmd, path);
		return EXIT_FAILURE;
	}

	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) {
		errmsg(cmd, path);
		return EXIT_FAILURE;
	}

	int ret = EXIT_SUCCESS;

	if (bind(fd, (struct sockaddr *)&sock, sizeof(sock)) != 0) {
		errmsg(cmd, path);
		ret = EXIT_FAILURE;
	}

	if (close(fd) != 0) {
		errmsg(cmd, path);
		ret = EXIT_FAILURE;
	}

	return ret;
}
