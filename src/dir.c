// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

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

#include <sys/syscall.h>

#if __has_feature(memory_sanitizer)
#	include <sanitizer/msan_interface.h>
#endif

/** Directory entry type for bfs_getdents() */
typedef struct dirent64 sys_dirent;

/** getdents() syscall wrapper. */
static ssize_t bfs_getdents(int fd, void *buf, size_t size) {
#if __has_feature(memory_sanitizer)
	__msan_allocated_memory(buf, size);
#endif

#if __GLIBC__ && !__GLIBC_PREREQ(2, 30)
	ssize_t ret = syscall(__NR_getdents64, fd, buf, size);
#else
	ssize_t ret = getdents64(fd, buf, size);
#endif

#if __has_feature(memory_sanitizer)
	if (ret > 0) {
		__msan_unpoison(buf, ret);
	}
#endif

	return ret;
}

#else // !__linux__
typedef struct dirent sys_dirent;
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

#if __linux__
#	define BUF_SIZE ((64 << 10) - sizeof(struct bfs_dir))
#endif

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

/** Convert de->d_type to a bfs_type, if it exists. */
static enum bfs_type bfs_d_type(const sys_dirent *de) {
#ifdef DTTOIF
	return bfs_mode_to_type(DTTOIF(de->d_type));
#else
	return BFS_UNKNOWN;
#endif
}

/** Read a single directory entry. */
static int bfs_getdent(struct bfs_dir *dir, const sys_dirent **de) {
#if __linux__
	char *buf = (char *)(dir + 1);

	if (dir->pos >= dir->size) {
		ssize_t ret = bfs_getdents(dir->fd, buf, BUF_SIZE);
		if (ret <= 0) {
			return ret;
		}
		dir->pos = 0;
		dir->size = ret;
	}

	*de = (void *)(buf + dir->pos);
	dir->pos += (*de)->d_reclen;
	return 1;
#else
	errno = 0;
	*de = readdir(dir->dir);
	if (*de) {
		return 1;
	} else if (errno == 0) {
		return 0;
	} else {
		return -1;
	}
#endif
}

/** Check if a name is . or .. */
static bool is_dot(const char *name) {
	return name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'));
}

int bfs_readdir(struct bfs_dir *dir, struct bfs_dirent *de) {
	while (true) {
		const sys_dirent *sysde;
		int ret = bfs_getdent(dir, &sysde);
		if (ret <= 0) {
			return ret;
		}

		if (is_dot(sysde->d_name)) {
			continue;
		}

		if (de) {
			de->type = bfs_d_type(sysde);
			de->name = sysde->d_name;
		}

		return 1;
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
