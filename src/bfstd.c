// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include "bfstd.h"
#include "config.h"
#include "diag.h"
#include "xregex.h"
#include <errno.h>
#include <fcntl.h>
#include <langinfo.h>
#include <nl_types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <wchar.h>

#if BFS_USE_SYS_SYSMACROS_H
#  include <sys/sysmacros.h>
#elif BFS_USE_SYS_MKDEV_H
#  include <sys/mkdev.h>
#endif

#if BFS_USE_UTIL_H
#  include <util.h>
#endif

bool is_nonexistence_error(int error) {
	return error == ENOENT || errno == ENOTDIR;
}

char *xdirname(const char *path) {
	size_t i = xbaseoff(path);

	// Skip trailing slashes
	while (i > 0 && path[i - 1] == '/') {
		--i;
	}

	if (i > 0) {
		return strndup(path, i);
	} else if (path[i] == '/') {
		return strdup("/");
	} else {
		return strdup(".");
	}
}

char *xbasename(const char *path) {
	size_t i = xbaseoff(path);
	size_t len = strcspn(path + i, "/");
	if (len > 0) {
		return strndup(path + i, len);
	} else if (path[i] == '/') {
		return strdup("/");
	} else {
		return strdup(".");
	}
}

size_t xbaseoff(const char *path) {
	size_t i = strlen(path);

	// Skip trailing slashes
	while (i > 0 && path[i - 1] == '/') {
		--i;
	}

	// Find the beginning of the name
	while (i > 0 && path[i - 1] != '/') {
		--i;
	}

	// Skip leading slashes
	while (path[i] == '/' && path[i + 1]) {
		++i;
	}

	return i;
}

FILE *xfopen(const char *path, int flags) {
	char mode[4];

	switch (flags & O_ACCMODE) {
	case O_RDONLY:
		strcpy(mode, "rb");
		break;
	case O_WRONLY:
		strcpy(mode, "wb");
		break;
	case O_RDWR:
		strcpy(mode, "r+b");
		break;
	default:
		bfs_bug("Invalid access mode");
		errno = EINVAL;
		return NULL;
	}

	if (flags & O_APPEND) {
		mode[0] = 'a';
	}

	int fd;
	if (flags & O_CREAT) {
		fd = open(path, flags, 0666);
	} else {
		fd = open(path, flags);
	}

	if (fd < 0) {
		return NULL;
	}

	FILE *ret = fdopen(fd, mode);
	if (!ret) {
		close_quietly(fd);
		return NULL;
	}

	return ret;
}

char *xgetdelim(FILE *file, char delim) {
	char *chunk = NULL;
	size_t n = 0;
	ssize_t len = getdelim(&chunk, &n, delim, file);
	if (len >= 0) {
		if (chunk[len] == delim) {
			chunk[len] = '\0';
		}
		return chunk;
	} else {
		free(chunk);
		if (!ferror(file)) {
			errno = 0;
		}
		return NULL;
	}
}

/** Compile and execute a regular expression for xrpmatch(). */
static int xrpregex(nl_item item, const char *response) {
	const char *pattern = nl_langinfo(item);
	if (!pattern) {
		return -1;
	}

	struct bfs_regex *regex;
	int ret = bfs_regcomp(&regex, pattern, BFS_REGEX_POSIX_EXTENDED, 0);
	if (ret == 0) {
		ret = bfs_regexec(regex, response, 0);
	}

	bfs_regfree(regex);
	return ret;
}

/** Check if a response is affirmative or negative. */
static int xrpmatch(const char *response) {
	int ret = xrpregex(NOEXPR, response);
	if (ret > 0) {
		return 0;
	} else if (ret < 0) {
		return -1;
	}

	ret = xrpregex(YESEXPR, response);
	if (ret > 0) {
		return 1;
	} else if (ret < 0) {
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

int ynprompt(void) {
	fflush(stderr);

	char *line = xgetdelim(stdin, '\n');
	int ret = line ? xrpmatch(line) : -1;
	free(line);
	return ret;
}

/** Get the single character describing the given file type. */
static char type_char(mode_t mode) {
	switch (mode & S_IFMT) {
	case S_IFREG:
		return '-';
	case S_IFBLK:
		return 'b';
	case S_IFCHR:
		return 'c';
	case S_IFDIR:
		return 'd';
	case S_IFLNK:
		return 'l';
	case S_IFIFO:
		return 'p';
	case S_IFSOCK:
		return 's';
#ifdef S_IFDOOR
	case S_IFDOOR:
		return 'D';
#endif
#ifdef S_IFPORT
	case S_IFPORT:
		return 'P';
#endif
#ifdef S_IFWHT
	case S_IFWHT:
		return 'w';
#endif
	}

	return '?';
}

void xstrmode(mode_t mode, char str[11]) {
	strcpy(str, "----------");

	str[0] = type_char(mode);

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

dev_t xmakedev(int ma, int mi) {
#ifdef makedev
	return makedev(ma, mi);
#else
	return (ma << 8) | mi;
#endif
}

int xmajor(dev_t dev) {
#ifdef major
	return major(dev);
#else
	return dev >> 8;
#endif
}

int xminor(dev_t dev) {
#ifdef minor
	return minor(dev);
#else
	return dev & 0xFF;
#endif
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
		close_quietly(ret);
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
		close_quietly(pipefd[1]);
		close_quietly(pipefd[0]);
		return -1;
	}

	return 0;
#endif
}

size_t xread(int fd, void *buf, size_t nbytes) {
	size_t count = 0;

	while (count < nbytes) {
		ssize_t ret = read(fd, (char *)buf + count, nbytes - count);
		if (ret < 0) {
			if (errno == EINTR) {
				continue;
			} else {
				break;
			}
		} else if (ret == 0) {
			// EOF
			errno = 0;
			break;
		} else {
			count += ret;
		}
	}

	return count;
}

size_t xwrite(int fd, const void *buf, size_t nbytes) {
	size_t count = 0;

	while (count < nbytes) {
		ssize_t ret = write(fd, (const char *)buf + count, nbytes - count);
		if (ret < 0) {
			if (errno == EINTR) {
				continue;
			} else {
				break;
			}
		} else if (ret == 0) {
			// EOF?
			errno = 0;
			break;
		} else {
			count += ret;
		}
	}

	return count;
}

void close_quietly(int fd) {
	int error = errno;
	xclose(fd);
	errno = error;
}

int xclose(int fd) {
	int ret = close(fd);
	if (ret != 0) {
		bfs_verify(errno != EBADF);
	}
	return ret;
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

char *xconfstr(int name) {
#if __ANDROID__
	errno = ENOTSUP;
	return NULL;
#else
	size_t len = confstr(name, NULL, 0);
	if (len == 0) {
		return NULL;
	}

	char *str = malloc(len);
	if (!str) {
		return NULL;
	}

	if (confstr(name, str, len) != len) {
		free(str);
		return NULL;
	}

	return str;
#endif // !__ANDROID__
}

char *xreadlinkat(int fd, const char *path, size_t size) {
	ssize_t len;
	char *name = NULL;

	if (size == 0) {
		size = 64;
	} else {
		++size; // NUL terminator
	}

	while (true) {
		char *new_name = realloc(name, size);
		if (!new_name) {
			goto error;
		}
		name = new_name;

		len = readlinkat(fd, path, name, size);
		if (len < 0) {
			goto error;
		} else if ((size_t)len >= size) {
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

int xstrtofflags(const char **str, unsigned long long *set, unsigned long long *clear) {
#if BSD && !__GNU__
	char *str_arg = (char *)*str;
	unsigned long set_arg = 0;
	unsigned long clear_arg = 0;

#if __NetBSD__
	int ret = string_to_flags(&str_arg, &set_arg, &clear_arg);
#else
	int ret = strtofflags(&str_arg, &set_arg, &clear_arg);
#endif

	*str = str_arg;
	*set = set_arg;
	*clear = clear_arg;

	if (ret != 0) {
		errno = EINVAL;
	}
	return ret;
#else // !BSD
	errno = ENOTSUP;
	return -1;
#endif
}

size_t xstrwidth(const char *str) {
	size_t len = strlen(str);
	size_t ret = 0;

	mbstate_t mb;
	memset(&mb, 0, sizeof(mb));

	while (len > 0) {
		wchar_t wc;
		size_t mblen = mbrtowc(&wc, str, len, &mb);
		int cwidth;
		if (mblen == (size_t)-1) {
			// Invalid byte sequence, assume a single-width '?'
			mblen = 1;
			cwidth = 1;
			memset(&mb, 0, sizeof(mb));
		} else if (mblen == (size_t)-2) {
			// Incomplete byte sequence, assume a single-width '?'
			mblen = len;
			cwidth = 1;
		} else {
			cwidth = wcwidth(wc);
			if (cwidth < 0) {
				cwidth = 0;
			}
		}

		str += mblen;
		len -= mblen;
		ret += cwidth;
	}

	return ret;
}
