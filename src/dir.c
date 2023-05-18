// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include "dir.h"
#include "bfstd.h"
#include "config.h"
#include "diag.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef BFS_GETDENTS
#  define BFS_GETDENTS (__linux__ || __FreeBSD__)
#endif

#if BFS_GETDENTS
#  if __has_feature(memory_sanitizer)
#    include <sanitizer/msan_interface.h>
#  endif
#  if __linux__
#    include <sys/syscall.h>
#  endif

/** getdents() syscall wrapper. */
static ssize_t bfs_getdents(int fd, void *buf, size_t size) {
#if __has_feature(memory_sanitizer)
	__msan_allocated_memory(buf, size);
#endif

#if __linux__ && __GLIBC__ && !__GLIBC_PREREQ(2, 30)
	ssize_t ret = syscall(SYS_getdents64, fd, buf, size);
#elif __linux__
	ssize_t ret = getdents64(fd, buf, size);
#else
	ssize_t ret = getdents(fd, buf, size);
#endif

#if __has_feature(memory_sanitizer)
	if (ret > 0) {
		__msan_unpoison(buf, ret);
	}
#endif

	return ret;
}

#endif // BFS_GETDENTS

#if BFS_GETDENTS && __linux__
/** Directory entry type for bfs_getdents() */
typedef struct dirent64 sys_dirent;
#else
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
#if BFS_GETDENTS
	alignas(sys_dirent) int fd;
	unsigned short pos;
	unsigned short size;
	// sys_dirent buf[];
#else
	DIR *dir;
#endif
};

#if BFS_GETDENTS
#  define DIR_SIZE (64 << 10)
#  define BUF_SIZE (DIR_SIZE - sizeof(struct bfs_dir))
#else
#  define DIR_SIZE sizeof(struct bfs_dir)
#endif

struct bfs_dir *bfs_opendir(int at_fd, const char *at_path) {
	struct bfs_dir *dir = malloc(DIR_SIZE);
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

#if BFS_GETDENTS
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
#endif

	return dir;
}

int bfs_dirfd(const struct bfs_dir *dir) {
#if BFS_GETDENTS
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
#if BFS_GETDENTS
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

/** Skip ".", "..", and deleted/empty dirents. */
static bool skip_dirent(const sys_dirent *de) {
#if __FreeBSD__
	// NFS mounts on FreeBSD can return empty dirents with inode number 0
	if (de->d_ino == 0) {
		return true;
	}
#endif

	const char *name = de->d_name;
	return name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'));
}

int bfs_readdir(struct bfs_dir *dir, struct bfs_dirent *de) {
	while (true) {
		const sys_dirent *sysde;
		int ret = bfs_getdent(dir, &sysde);
		if (ret <= 0) {
			return ret;
		}

		if (skip_dirent(sysde)) {
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
#if BFS_GETDENTS
	int ret = xclose(dir->fd);
#else
	int ret = closedir(dir->dir);
	bfs_verify(ret == 0 || errno != EBADF);
#endif
	free(dir);
	return ret;
}

int bfs_freedir(struct bfs_dir *dir) {
#if BFS_GETDENTS
	int ret = dir->fd;
	free(dir);
#elif __FreeBSD__
	int ret = fdclosedir(dir->dir);
	free(dir);
#else
	int ret = dup_cloexec(dirfd(dir->dir));
	int error = errno;
	bfs_closedir(dir);
	errno = error;
#endif
	return ret;
}
