/****************************************************************************
 * bfs                                                                      *
 * Copyright (C) 2021 Tavian Barnes <tavianator@tavianator.com>             *
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
#include "util.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>

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
	DIR *dir;
	struct dirent *ent;
};

struct bfs_dir *bfs_opendir(int at_fd, const char *at_path) {
	struct bfs_dir *dir = malloc(sizeof(*dir));
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

	dir->dir = fdopendir(fd);
	if (!dir->dir) {
		int error = errno;
		close(fd);
		free(dir);
		errno = error;
		return NULL;
	}

	dir->ent = NULL;

	return dir;
}

int bfs_dirfd(const struct bfs_dir *dir) {
	return dirfd(dir->dir);
}

static enum bfs_type dirent_type(const struct dirent *de) {
#if defined(_DIRENT_HAVE_D_TYPE) || defined(DT_UNKNOWN)
	switch (de->d_type) {
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
#endif

	return BFS_UNKNOWN;
}

int bfs_readdir(struct bfs_dir *dir, struct bfs_dirent *dirent) {
	while (true) {
		errno = 0;
		dir->ent = readdir(dir->dir);
		if (dir->ent) {
			const char *name = dir->ent->d_name;
			if (name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'))) {
				continue;
			}
			if (dirent) {
				dirent->type = dirent_type(dir->ent);
				dirent->name = name;
			}
			return 1;
		} else if (errno != 0) {
			return -1;
		} else {
			return 0;
		}
	}
}

int bfs_closedir(struct bfs_dir *dir) {
	int ret = closedir(dir->dir);
	free(dir);
	return ret;
}

int bfs_freedir(struct bfs_dir *dir) {
	int ret = dup_cloexec(dirfd(dir->dir));
	bfs_closedir(dir);
	return ret;
}
