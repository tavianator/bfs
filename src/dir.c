/****************************************************************************
 * bfs                                                                      *
 * Copyright (C) 2021-2022 Tavian Barnes <tavianator@tavianator.com>        *
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

#include "dir.h"
#include "bfstd.h"
#include "config.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#if __linux__
#	include <sys/syscall.h>
#endif

enum bfs_type bfs_mode_to_type(mode_t mode) {
	switch (mode & S_IFMT) {
#ifdef S_IFBLK
	case S_IFBLK:
		return BFS_BLK;
#endif
#ifdef S_IFCHR
	case S_IFCHR:
		return BFS_CHR;
#endif
#ifdef S_IFDIR
	case S_IFDIR:
		return BFS_DIR;
#endif
#ifdef S_IFDOOR
	case S_IFDOOR:
		return BFS_DOOR;
#endif
#ifdef S_IFIFO
	case S_IFIFO:
		return BFS_FIFO;
#endif
#ifdef S_IFLNK
	case S_IFLNK:
		return BFS_LNK;
#endif
#ifdef S_IFPORT
	case S_IFPORT:
		return BFS_PORT;
#endif
#ifdef S_IFREG
	case S_IFREG:
		return BFS_REG;
#endif
#ifdef S_IFSOCK
	case S_IFSOCK:
		return BFS_SOCK;
#endif
#ifdef S_IFWHT
	case S_IFWHT:
		return BFS_WHT;
#endif

	default:
		return BFS_UNKNOWN;
	}
}

#if __linux__
/**
 * This is not defined in the kernel headers for some reason, callers have to
 * define it themselves.
 */
struct linux_dirent64 {
	ino64_t d_ino;
	off64_t d_off;
	unsigned short d_reclen;
	unsigned char d_type;
	char d_name[];
};

// Make the whole allocation 64k
#define BUF_SIZE ((64 << 10) - 8)
#endif

struct bfs_dir {
#if __linux__
	int fd;
	unsigned short pos;
	unsigned short size;
#else
	DIR *dir;
	struct dirent *de;
#endif
};

struct bfs_dir *bfs_opendir(int at_fd, const char *at_path) {
#if __linux__
	struct bfs_dir *dir = malloc(sizeof(*dir) + BUF_SIZE);
#else
	struct bfs_dir *dir = malloc(sizeof(*dir));
#endif
	if (!dir) {
		return NULL;
	}

	int fd;
	if (at_path) {
		fd = openat(at_fd, at_path, O_RDONLY | O_CLOEXEC | O_DIRECTORY);
	} else if (at_fd >= 0) {
		fd = at_fd;
	} else {
		free(dir);
		errno = EBADF;
		return NULL;
	}

	if (fd < 0) {
		free(dir);
		return NULL;
	}

#if __linux__
	dir->fd = fd;
	dir->pos = 0;
	dir->size = 0;
#else
	dir->dir = fdopendir(fd);
	if (!dir->dir) {
		if (at_path) {
			close_quietly(fd);
		}
		free(dir);
		return NULL;
	}

	dir->de = NULL;
#endif // __linux__

	return dir;
}

int bfs_dirfd(const struct bfs_dir *dir) {
#if __linux__
	return dir->fd;
#else
	return dirfd(dir->dir);
#endif
}

/** Convert a dirent type to a bfs_type. */
static enum bfs_type translate_type(int d_type) {
	switch (d_type) {
#ifdef DT_BLK
	case DT_BLK:
		return BFS_BLK;
#endif
#ifdef DT_CHR
	case DT_CHR:
		return BFS_CHR;
#endif
#ifdef DT_DIR
	case DT_DIR:
		return BFS_DIR;
#endif
#ifdef DT_DOOR
	case DT_DOOR:
		return BFS_DOOR;
#endif
#ifdef DT_FIFO
	case DT_FIFO:
		return BFS_FIFO;
#endif
#ifdef DT_LNK
	case DT_LNK:
		return BFS_LNK;
#endif
#ifdef DT_PORT
	case DT_PORT:
		return BFS_PORT;
#endif
#ifdef DT_REG
	case DT_REG:
		return BFS_REG;
#endif
#ifdef DT_SOCK
	case DT_SOCK:
		return BFS_SOCK;
#endif
#ifdef DT_WHT
	case DT_WHT:
		return BFS_WHT;
#endif
	}

	return BFS_UNKNOWN;
}

#if !__linux__
/** Get the type from a struct dirent if it exists, and convert it. */
static enum bfs_type dirent_type(const struct dirent *de) {
#if defined(_DIRENT_HAVE_D_TYPE) || defined(DT_UNKNOWN)
	return translate_type(de->d_type);
#else
	return BFS_UNKNOWN;
#endif
}
#endif

/** Check if a name is . or .. */
static bool is_dot(const char *name) {
	return name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'));
}

int bfs_readdir(struct bfs_dir *dir, struct bfs_dirent *de) {
	while (true) {
#if __linux__
		char *buf = (char *)(dir + 1);

		if (dir->pos >= dir->size) {
#if __has_feature(memory_sanitizer)
			// Make sure msan knows the buffer is initialized
			memset(buf, 0, BUF_SIZE);
#endif

			ssize_t size = syscall(__NR_getdents64, dir->fd, buf, BUF_SIZE);
			if (size <= 0) {
				return size;
			}
			dir->pos = 0;
			dir->size = size;
		}

		const struct linux_dirent64 *lde = (void *)(buf + dir->pos);
		dir->pos += lde->d_reclen;

		if (is_dot(lde->d_name)) {
			continue;
		}

		if (de) {
			de->type = translate_type(lde->d_type);
			de->name = lde->d_name;
		}

		return 1;
#else // !__linux__
		errno = 0;
		dir->de = readdir(dir->dir);
		if (dir->de) {
			if (is_dot(dir->de->d_name)) {
				continue;
			}
			if (de) {
				de->type = dirent_type(dir->de);
				de->name = dir->de->d_name;
			}
			return 1;
		} else if (errno != 0) {
			return -1;
		} else {
			return 0;
		}
#endif // !__linux__
	}
}

int bfs_closedir(struct bfs_dir *dir) {
#if __linux__
	int ret = xclose(dir->fd);
#else
	int ret = closedir(dir->dir);
#endif
	free(dir);
	return ret;
}

int bfs_freedir(struct bfs_dir *dir) {
#if __linux__
	int ret = dir->fd;
	free(dir);
	return ret;
#elif __FreeBSD__
	int ret = fdclosedir(dir->dir);
	free(dir);
	return ret;
#else
	int ret = dup_cloexec(dirfd(dir->dir));
	bfs_closedir(dir);
	return ret;
#endif
}
