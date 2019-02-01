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

/**
 * There's no standard Unix utility that creates a socket file, so this small
 * program does the job.
 */

#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
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
	char *copy = strdup(path);
	if (!copy) {
		return -1;
	}
	const char *dir = dirname(copy);

	int ret = chdir(dir);

	int error = errno;
	free(copy);
	errno = error;

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

	char *copy = strdup(path);
	if (!copy) {
		return -1;
	}
	const char *base = basename(copy);

	len = strlen(base);
	if (len >= sizeof(sock->sun_path)) {
		free(copy);
		errno = ENAMETOOLONG;
		return -1;
	}

	sock->sun_family = AF_UNIX;
	memcpy(sock->sun_path, base, len + 1);
	free(copy);
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
