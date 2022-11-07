/****************************************************************************
 * bfs                                                                      *
 * Copyright (C) 2022 Tavian Barnes <tavianator@tavianator.com>             *
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

#include "../src/xtime.h"
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/** Check if a path has a trailing slash. */
static bool trailing_slash(const char *path) {
	size_t len = strlen(path);
	return len > 0 && path[len - 1] == '/';
}

/** Create any parent directories of the given path. */
static int mkdirs(const char *path, mode_t mode) {
	char *copy = strdup(path);
	if (!copy) {
		return -1;
	}

	int ret = -1;
	char *cur = copy + strspn(copy, "/");
	while (true) {
		cur += strcspn(cur, "/");

		char *next = cur + strspn(cur, "/");
		if (!*next) {
			ret = 0;
			break;
		}

		*cur = '\0';
		if (mkdir(copy, mode) != 0 && errno != EEXIST) {
			break;
		}
		*cur = '/';
		cur = next;
	}

	free(copy);
	return ret;
}

/** Touch one path. */
static int xtouch(const char *path, const struct timespec times[2], int flags, mode_t mode, bool parents, mode_t pmode) {
	int ret = utimensat(AT_FDCWD, path, times, flags);
	if (ret == 0 || errno != ENOENT) {
		return ret;
	}

	if (parents && mkdirs(path, pmode) != 0) {
		return -1;
	}

	if (trailing_slash(path)) {
		if (mkdir(path, mode) != 0) {
			return -1;
		}

		return utimensat(AT_FDCWD, path, times, flags);
	} else {
		int fd = open(path, O_WRONLY | O_CREAT, mode);
		if (fd < 0) {
			return -1;
		}

		if (futimens(fd, times) != 0) {
			int error = errno;
			close(fd);
			errno = error;
			return -1;
		}

		return close(fd);
	}
}

int main(int argc, char *argv[]) {
	int flags = 0;
	bool atime = false, mtime = false;
	bool parents = false;
	const char *mstr = NULL;
	const char *tstr = NULL;

	const char *cmd = argc > 0 ? argv[0] : "xtouch";
	int c;
	while (c = getopt(argc, argv, ":M:ahmpt:"), c != -1) {
		switch (c) {
		case 'M':
			mstr = optarg;
			break;
		case 'a':
			atime = true;
			break;
		case 'h':
			flags = AT_SYMLINK_NOFOLLOW;
			break;
		case 'm':
			mtime = true;
			break;
		case 'p':
			parents = true;
			break;
		case 't':
			tstr = optarg;
			break;
		case ':':
			fprintf(stderr, "%s: Missing argument to -%c\n", cmd, optopt);
			return EXIT_FAILURE;
		case '?':
			fprintf(stderr, "%s: Unrecognized option -%c\n", cmd, optopt);
			return EXIT_FAILURE;
		}
	}

	mode_t mask = umask(0);
	mode_t fmode = 0666 & ~mask;
	mode_t dmode = 0777 & ~mask;
	mode_t pmode = 0777 & ~mask;
	if (mstr) {
		char *end;
		long mode = strtol(mstr, &end, 8);
		if (*mstr && !*end && mode >= 0 && mode < 01000) {
			fmode = dmode = mode;
		} else {
			fprintf(stderr, "%s: Invalid mode '%s'\n", cmd, mstr);
			return EXIT_FAILURE;
		}
	}

	struct timespec ts;
	if (tstr) {
		if (xgetdate(tstr, &ts) != 0) {
			fprintf(stderr, "%s: Parsing time '%s' failed: %s\n", cmd, tstr, strerror(errno));
			return EXIT_FAILURE;
		}
	} else {
		// Don't use UTIME_NOW, so that multiple paths all get the same timestamp
		if (xgettime(&ts) != 0) {
			perror("xgettime()");
			return EXIT_FAILURE;
		}
	}

	struct timespec times[2] = {
		{ .tv_nsec = UTIME_OMIT },
		{ .tv_nsec = UTIME_OMIT },
	};
	if (!atime && !mtime) {
		atime = true;
		mtime = true;
	}
	if (atime) {
		times[0] = ts;
	}
	if (mtime) {
		times[1] = ts;
	}

	if (optind >= argc) {
		fprintf(stderr, "%s: No files to touch\n", cmd);
		return EXIT_FAILURE;
	}

	int ret = EXIT_SUCCESS;
	for (; optind < argc; ++optind) {
		const char *path = argv[optind];
		bool isdir = trailing_slash(path);
		if (xtouch(path, times, flags, isdir ? dmode : fmode, parents, pmode) != 0) {
			fprintf(stderr, "%s: Touching '%s' failed: %s\n", cmd, path, strerror(errno));
			ret = EXIT_FAILURE;
		}
	}
	return ret;
}
