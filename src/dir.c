// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include "dir.h"
#include "alloc.h"
#include "bfstd.h"
#include "config.h"
#include "diag.h"
#include "sanity.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#if BFS_USE_GETDENTS
#  if __linux__
#    include <sys/syscall.h>
#  endif

/** getdents() syscall wrapper. */
static ssize_t bfs_getdents(int fd, void *buf, size_t size) {
	sanitize_uninit(buf, size);

#if __linux__ && __GLIBC__ && !__GLIBC_PREREQ(2, 30)
	ssize_t ret = syscall(SYS_getdents64, fd, buf, size);
#elif __linux__
	ssize_t ret = getdents64(fd, buf, size);
#else
	ssize_t ret = getdents(fd, buf, size);
#endif

	if (ret > 0) {
		sanitize_init(buf, ret);
	}

	return ret;
}

#endif // BFS_USE_GETDENTS

#if BFS_USE_GETDENTS && __linux__
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
#if BFS_USE_GETDENTS
	alignas(sys_dirent) int fd;
	unsigned short pos;
	unsigned short size;
	// sys_dirent buf[];
#else
	DIR *dir;
	struct dirent *de;
#endif

	bool eof;
};

#if BFS_USE_GETDENTS
#  define DIR_SIZE (64 << 10)
#  define BUF_SIZE (DIR_SIZE - sizeof(struct bfs_dir))
#else
#  define DIR_SIZE sizeof(struct bfs_dir)
#endif

struct bfs_dir *bfs_allocdir(void) {
	return malloc(DIR_SIZE);
}

void bfs_dir_arena(struct arena *arena) {
	arena_init(arena, alignof(struct bfs_dir), DIR_SIZE);
}

int bfs_opendir(struct bfs_dir *dir, int at_fd, const char *at_path) {
	int fd;
	if (at_path) {
		fd = openat(at_fd, at_path, O_RDONLY | O_CLOEXEC | O_DIRECTORY);
		if (fd < 0) {
			return -1;
		}
	} else if (at_fd >= 0) {
		fd = at_fd;
	} else {
		errno = EBADF;
		return -1;
	}

#if BFS_USE_GETDENTS
	dir->fd = fd;
	dir->pos = 0;
	dir->size = 0;
#else
	dir->dir = fdopendir(fd);
	if (!dir->dir) {
		if (at_path) {
			close_quietly(fd);
		}
		return -1;
	}
	dir->de = NULL;
#endif

	dir->eof = false;
	return 0;
}

int bfs_dirfd(const struct bfs_dir *dir) {
#if BFS_USE_GETDENTS
	return dir->fd;
#else
	return dirfd(dir->dir);
#endif
}

int bfs_polldir(struct bfs_dir *dir) {
#if BFS_USE_GETDENTS
	if (dir->pos < dir->size) {
		return 1;
	} else if (dir->eof) {
		return 0;
	}

	char *buf = (char *)(dir + 1);
	ssize_t size = bfs_getdents(dir->fd, buf, BUF_SIZE);
	if (size == 0) {
		dir->eof = true;
		return 0;
	} else if (size < 0) {
		return -1;
	}

	dir->pos = 0;
	dir->size = size;

	// Like read(), getdents() doesn't indicate EOF until another call returns zero.
	// Check that eagerly here to hopefully avoid a syscall in the last bfs_readdir().
	size_t rest = BUF_SIZE - size;
	if (rest >= sizeof(sys_dirent)) {
		size = bfs_getdents(dir->fd, buf + size, rest);
		if (size > 0) {
			dir->size += size;
		} else if (size == 0) {
			dir->eof = true;
		}
	}

	return 1;
#else // !BFS_USE_GETDENTS
	if (dir->de) {
		return 1;
	} else if (dir->eof) {
		return 0;
	}

	errno = 0;
	dir->de = readdir(dir->dir);
	if (dir->de) {
		return 1;
	} else if (errno == 0) {
		dir->eof = true;
		return 0;
	} else {
		return -1;
	}
#endif
}

/** Read a single directory entry. */
static int bfs_getdent(struct bfs_dir *dir, const sys_dirent **de) {
	int ret = bfs_polldir(dir);
	if (ret > 0) {
#if BFS_USE_GETDENTS
		char *buf = (char *)(dir + 1);
		*de = (const sys_dirent *)(buf + dir->pos);
		dir->pos += (*de)->d_reclen;
#else
		*de = dir->de;
		dir->de = NULL;
#endif
	}
	return ret;
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

/** Convert de->d_type to a bfs_type, if it exists. */
static enum bfs_type bfs_d_type(const sys_dirent *de) {
#ifdef DTTOIF
	return bfs_mode_to_type(DTTOIF(de->d_type));
#else
	return BFS_UNKNOWN;
#endif
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
#if BFS_USE_GETDENTS
	int ret = xclose(dir->fd);
#else
	int ret = closedir(dir->dir);
	if (ret != 0) {
		bfs_verify(errno != EBADF);
	}
#endif

	sanitize_uninit(dir, DIR_SIZE);
	return ret;
}

#if BFS_USE_UNWRAPDIR
int bfs_unwrapdir(struct bfs_dir *dir) {
#if BFS_USE_GETDENTS
	int ret = dir->fd;
#elif __FreeBSD__
	int ret = fdclosedir(dir->dir);
#endif

	sanitize_uninit(dir, DIR_SIZE);
	return ret;
}
#endif
