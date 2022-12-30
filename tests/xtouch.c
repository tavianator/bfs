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

/** Parsed xtouch arguments. */
struct args {
	/** Simple flags. */
	enum {
		/** Don't create nonexistent files (-c). */
		NO_CREATE = 1 << 0,
		/** Don't follow symlinks (-h). */
		NO_FOLLOW = 1 << 1,
		/** Create any missing parent directories (-p). */
		CREATE_PARENTS = 1 << 2,
	} flags;

	/** Timestamps (-r|-t|-d). */
	struct timespec times[2];

	/** File creation mode (-M; default 0666 & ~umask). */
	mode_t fmode;
	/** Directory creation mode (-M; default 0777 & ~umask). */
	mode_t dmode;
	/** Parent directory creation mode (0777 & ~umask). */
	mode_t pmode;
};

/** Compute flags for fstatat()/utimensat(). */
static int at_flags(const struct args *args) {
	if (args->flags & NO_FOLLOW) {
		return AT_SYMLINK_NOFOLLOW;
	} else {
		return 0;
	}
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
static int xtouch(const struct args *args, const char *path) {
	int ret = utimensat(AT_FDCWD, path, args->times, at_flags(args));
	if (ret == 0 || errno != ENOENT) {
		return ret;
	}

	if (args->flags & NO_CREATE) {
		return 0;
	} else if (args->flags & CREATE_PARENTS) {
		if (mkdirs(path, args->pmode) != 0) {
			return -1;
		}
	}

	size_t len = strlen(path);
	if (len > 0 && path[len - 1] == '/') {
		if (mkdir(path, args->dmode) != 0) {
			return -1;
		}

		return utimensat(AT_FDCWD, path, args->times, at_flags(args));
	} else {
		int fd = open(path, O_WRONLY | O_CREAT, args->fmode);
		if (fd < 0) {
			return -1;
		}

		if (futimens(fd, args->times) != 0) {
			int error = errno;
			close(fd);
			errno = error;
			return -1;
		}

		return close(fd);
	}
}

int main(int argc, char *argv[]) {
	mode_t mask = umask(0);

	struct args args = {
		.flags = 0,
		.times = {
			{ .tv_nsec = UTIME_OMIT },
			{ .tv_nsec = UTIME_OMIT },
		},
		.fmode = 0666 & ~mask,
		.dmode = 0777 & ~mask,
		.pmode = 0777 & ~mask,
	};

	bool atime = false, mtime = false;
	const char *darg = NULL;
	const char *marg = NULL;
	const char *rarg = NULL;

	const char *cmd = argc > 0 ? argv[0] : "xtouch";
	int c;
	while (c = getopt(argc, argv, ":M:acd:hmpr:t:"), c != -1) {
		switch (c) {
		case 'M':
			marg = optarg;
			break;
		case 'a':
			atime = true;
			break;
		case 'c':
			args.flags |= NO_CREATE;
			break;
		case 'd':
		case 't':
			darg = optarg;
			break;
		case 'h':
			args.flags |= NO_FOLLOW;
			break;
		case 'm':
			mtime = true;
			break;
		case 'p':
			args.flags |= CREATE_PARENTS;
			break;
		case 'r':
			rarg = optarg;
			break;
		case ':':
			fprintf(stderr, "%s: Missing argument to -%c\n", cmd, optopt);
			return EXIT_FAILURE;
		case '?':
			fprintf(stderr, "%s: Unrecognized option -%c\n", cmd, optopt);
			return EXIT_FAILURE;
		}
	}

	if (marg) {
		char *end;
		long mode = strtol(marg, &end, 8);
		if (*marg && !*end && mode >= 0 && mode < 01000) {
			args.fmode = args.dmode = mode;
		} else {
			fprintf(stderr, "%s: Invalid mode '%s'\n", cmd, marg);
			return EXIT_FAILURE;
		}
	}

	struct timespec times[2];

	if (rarg) {
		struct stat buf;
		if (fstatat(AT_FDCWD, rarg, &buf, at_flags(&args)) != 0) {
			fprintf(stderr, "%s: '%s': %s\n", cmd, rarg, strerror(errno));
			return EXIT_FAILURE;
		}
		times[0] = buf.st_atim;
		times[1] = buf.st_mtim;
	} else if (darg) {
		if (xgetdate(darg, &times[0]) != 0) {
			fprintf(stderr, "%s: Parsing time '%s' failed: %s\n", cmd, darg, strerror(errno));
			return EXIT_FAILURE;
		}
		times[1] = times[0];
	} else {
		// Don't use UTIME_NOW, so that multiple paths all get the same timestamp
		if (xgettime(&times[0]) != 0) {
			perror("xgettime()");
			return EXIT_FAILURE;
		}
		times[1] = times[0];
	}

	if (!atime && !mtime) {
		atime = true;
		mtime = true;
	}
	if (atime) {
		args.times[0] = times[0];
	}
	if (mtime) {
		args.times[1] = times[1];
	}

	if (optind >= argc) {
		fprintf(stderr, "%s: No files to touch\n", cmd);
		return EXIT_FAILURE;
	}

	int ret = EXIT_SUCCESS;
	for (; optind < argc; ++optind) {
		const char *path = argv[optind];
		if (xtouch(&args, path) != 0) {
			fprintf(stderr, "%s: '%s': %s\n", cmd, path, strerror(errno));
			ret = EXIT_FAILURE;
		}
	}
	return ret;
}
