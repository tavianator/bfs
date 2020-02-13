/****************************************************************************
 * bfs                                                                      *
 * Copyright (C) 2016-2020 Tavian Barnes <tavianator@tavianator.com>        *
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

#include "util.h"
#include "bftw.h"
#include "dstring.h"
#include <errno.h>
#include <fcntl.h>
#include <langinfo.h>
#include <regex.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#if BFS_HAS_SYS_PARAM
#	include <sys/param.h>
#endif

#if BFS_HAS_SYS_SYSMACROS
#	include <sys/sysmacros.h>
#elif BFS_HAS_SYS_MKDEV
#	include <sys/mkdev.h>
#endif

int xreaddir(DIR *dir, struct dirent **de) {
	while (true) {
		errno = 0;
		*de = readdir(dir);

		if (*de) {
			const char *name = (*de)->d_name;
			if (name[0] != '.' || (name[1] != '\0' && (name[1] != '.' || name[2] != '\0'))) {
				return 0;
			}
		} else if (errno != 0) {
			return -1;
		} else {
			return 0;
		}
	}
}

char *xreadlinkat(int fd, const char *path, size_t size) {
	++size; // NUL-terminator
	ssize_t len;
	char *name = NULL;

	while (true) {
		char *new_name = realloc(name, size);
		if (!new_name) {
			goto error;
		}
		name = new_name;

		len = readlinkat(fd, path, name, size);
		if (len < 0) {
			goto error;
		} else if (len >= size) {
			size *= 2;
		} else {
			break;
		}
	}

	name[len] = '\0';
	return name;

error:
	free(name);
	return NULL;
}

bool isopen(int fd) {
	return fcntl(fd, F_GETFD) >= 0 || errno != EBADF;
}

int redirect(int fd, const char *path, int flags, ...) {
	mode_t mode = 0;
	if (flags & O_CREAT) {
		va_list args;
		va_start(args, flags);

		// Use int rather than mode_t, because va_arg must receive a
		// fully-promoted type
		mode = va_arg(args, int);

		va_end(args);
	}

	int ret = open(path, flags, mode);

	if (ret >= 0 && ret != fd) {
		int orig = ret;
		ret = dup2(orig, fd);
		close(orig);
	}

	return ret;
}

int dup_cloexec(int fd) {
#ifdef F_DUPFD_CLOEXEC
	return fcntl(fd, F_DUPFD_CLOEXEC, 0);
#else
	int ret = dup(fd);
	if (ret < 0) {
		return -1;
	}

	if (fcntl(ret, F_SETFD, FD_CLOEXEC) == -1) {
		close(ret);
		return -1;
	}

	return ret;
#endif
}

int pipe_cloexec(int pipefd[2]) {
#if __linux__ || (BSD && !__APPLE__)
	return pipe2(pipefd, O_CLOEXEC);
#else
	if (pipe(pipefd) != 0) {
		return -1;
	}

	if (fcntl(pipefd[0], F_SETFD, FD_CLOEXEC) == -1 || fcntl(pipefd[1], F_SETFD, FD_CLOEXEC) == -1) {
		int error = errno;
		close(pipefd[1]);
		close(pipefd[0]);
		errno = error;
		return -1;
	}

	return 0;
#endif
}

char *xregerror(int err, const regex_t *regex) {
	size_t len = regerror(err, regex, NULL, 0);
	char *str = malloc(len);
	if (str) {
		regerror(err, regex, str, len);
	}
	return str;
}

void format_mode(mode_t mode, char str[11]) {
	strcpy(str, "----------");

	switch (bftw_mode_typeflag(mode)) {
	case BFTW_REG:
		break;
	case BFTW_BLK:
		str[0] = 'b';
		break;
	case BFTW_CHR:
		str[0] = 'c';
		break;
	case BFTW_DIR:
		str[0] = 'd';
		break;
	case BFTW_DOOR:
		str[0] = 'D';
		break;
	case BFTW_LNK:
		str[0] = 'l';
		break;
	case BFTW_FIFO:
		str[0] = 'p';
		break;
	case BFTW_PORT:
		str[0] = 'P';
		break;
	case BFTW_SOCK:
		str[0] = 's';
		break;
	case BFTW_WHT:
		str[0] = 'w';
		break;
	case BFTW_UNKNOWN:
	case BFTW_ERROR:
		str[0] = '?';
		break;
	}

	if (mode & 00400) {
		str[1] = 'r';
	}
	if (mode & 00200) {
		str[2] = 'w';
	}
	if ((mode & 04100) == 04000) {
		str[3] = 'S';
	} else if (mode & 04000) {
		str[3] = 's';
	} else if (mode & 00100) {
		str[3] = 'x';
	}

	if (mode & 00040) {
		str[4] = 'r';
	}
	if (mode & 00020) {
		str[5] = 'w';
	}
	if ((mode & 02010) == 02000) {
		str[6] = 'S';
	} else if (mode & 02000) {
		str[6] = 's';
	} else if (mode & 00010) {
		str[6] = 'x';
	}

	if (mode & 00004) {
		str[7] = 'r';
	}
	if (mode & 00002) {
		str[8] = 'w';
	}
	if ((mode & 01001) == 01000) {
		str[9] = 'T';
	} else if (mode & 01000) {
		str[9] = 't';
	} else if (mode & 00001) {
		str[9] = 'x';
	}
}

const char *xbasename(const char *path) {
	const char *i;

	// Skip trailing slashes
	for (i = path + strlen(path); i > path && i[-1] == '/'; --i);

	// Find the beginning of the name
	for (; i > path && i[-1] != '/'; --i);

	// Skip leading slashes
	for (; i[0] == '/' && i[1]; ++i);

	return i;
}

int xfaccessat(int fd, const char *path, int amode) {
	int ret = faccessat(fd, path, amode, 0);

#ifdef AT_EACCESS
	// Some platforms, like Hurd, only support AT_EACCESS.  Other platforms,
	// like Android, don't support AT_EACCESS at all.
	if (ret != 0 && (errno == EINVAL || errno == ENOTSUP)) {
		ret = faccessat(fd, path, amode, AT_EACCESS);
	}
#endif

	return ret;
}

bool is_nonexistence_error(int error) {
	return error == ENOENT || errno == ENOTDIR;
}

/** Read a line from standard input. */
static char *xgetline(void) {
	char *line = dstralloc(0);
	if (!line) {
		return NULL;
	}

	while (true) {
		int c = getchar();
		if (c == '\n' || c == EOF) {
			break;
		}

		if (dstrapp(&line, c) != 0) {
			goto error;
		}
	}

	return line;

error:
	dstrfree(line);
	return NULL;
}

/** Compile and execute a regular expression for xrpmatch(). */
static int xrpregex(nl_item item, const char *response) {
	const char *pattern = nl_langinfo(item);
	if (!pattern) {
		return REG_BADPAT;
	}

	regex_t regex;
	int ret = regcomp(&regex, pattern, REG_EXTENDED);
	if (ret != 0) {
		return ret;
	}

	ret = regexec(&regex, response, 0, NULL, 0);
	regfree(&regex);
	return ret;
}

/** Check if a response is affirmative or negative. */
static int xrpmatch(const char *response) {
	int ret = xrpregex(NOEXPR, response);
	if (ret == 0) {
		return 0;
	} else if (ret != REG_NOMATCH) {
		return -1;
	}

	ret = xrpregex(YESEXPR, response);
	if (ret == 0) {
		return 1;
	} else if (ret != REG_NOMATCH) {
		return -1;
	}

	// Failsafe: always handle y/n
	char c = response[0];
	if (c == 'n' || c == 'N') {
		return 0;
	} else if (c == 'y' || c == 'Y') {
		return 1;
	} else {
		return -1;
	}
}

int ynprompt() {
	fflush(stderr);

	char *line = xgetline();
	int ret = line ? xrpmatch(line) : -1;
	dstrfree(line);
	return ret;
}

dev_t bfs_makedev(int ma, int mi) {
#ifdef makedev
	return makedev(ma, mi);
#else
	return (ma << 8) | mi;
#endif
}

int bfs_major(dev_t dev) {
#ifdef major
	return major(dev);
#else
	return dev >> 8;
#endif
}

int bfs_minor(dev_t dev) {
#ifdef minor
	return minor(dev);
#else
	return dev & 0xFF;
#endif
}
