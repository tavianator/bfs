/****************************************************************************
 * bfs                                                                      *
 * Copyright (C) 2018 Tavian Barnes <tavianator@tavianator.com>             *
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

#include "stat.h"
#include "util.h"
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef STATX_BASIC_STATS
#	define HAVE_STATX true
#elif __linux__
#	include <linux/stat.h>
#	include <sys/syscall.h>
#endif

#if HAVE_STATX || defined(__NR_statx)
#	define HAVE_BFS_STATX true
#	include <sys/sysmacros.h>
#endif

#if __APPLE__
#	define st_atim st_atimespec
#	define st_ctim st_ctimespec
#	define st_mtim st_mtimespec
#	define st_birthtim st_birthtimespec
#endif

/**
 * Check if we should retry a failed stat() due to a potentially broken link.
 */
static bool bfs_stat_retry(int ret, int at_flags, enum bfs_stat_flag flags) {
	return ret != 0
		&& !(at_flags & AT_SYMLINK_NOFOLLOW)
		&& (flags & BFS_STAT_BROKEN_OK)
		&& is_nonexistence_error(errno);
}

/**
 * Convert a struct stat to a struct bfs_stat.
 */
static void bfs_stat_convert(const struct stat *statbuf, struct bfs_stat *buf) {
	buf->mask = 0;

	buf->dev = statbuf->st_dev;
	buf->mask |= BFS_STAT_DEV;

	buf->ino = statbuf->st_ino;
	buf->mask |= BFS_STAT_INO;

	buf->mode = statbuf->st_mode;
	buf->mask |= BFS_STAT_TYPE | BFS_STAT_MODE;

	buf->nlink = statbuf->st_nlink;
	buf->mask |= BFS_STAT_NLINK;

	buf->gid = statbuf->st_gid;
	buf->mask |= BFS_STAT_GID;

	buf->uid = statbuf->st_uid;
	buf->mask |= BFS_STAT_UID;

	buf->size = statbuf->st_size;
	buf->mask |= BFS_STAT_SIZE;

	buf->blocks = statbuf->st_blocks;
	buf->mask |= BFS_STAT_BLOCKS;

	buf->atime = statbuf->st_atim;
	buf->mask |= BFS_STAT_ATIME;

	buf->ctime = statbuf->st_ctim;
	buf->mask |= BFS_STAT_CTIME;

	buf->mtime = statbuf->st_mtim;
	buf->mask |= BFS_STAT_MTIME;

#if __APPLE__ || __FreeBSD__ || __NetBSD__
	buf->btime = statbuf->st_birthtim;
	buf->mask |= BFS_STAT_BTIME;
#endif
}

/**
 * bfs_stat() implementation backed by stat().
 */
static int bfs_stat_impl(int at_fd, const char *at_path, int at_flags, enum bfs_stat_flag flags, struct bfs_stat *buf) {
	struct stat statbuf;
	int ret = fstatat(at_fd, at_path, &statbuf, at_flags);

	if (bfs_stat_retry(ret, at_flags, flags)) {
		at_flags |= AT_SYMLINK_NOFOLLOW;
		ret = fstatat(at_fd, at_path, &statbuf, at_flags);
	}

	if (ret == 0) {
		bfs_stat_convert(&statbuf, buf);
	}

	return ret;
}

#if HAVE_BFS_STATX

/**
 * Wrapper for the statx() system call, which had no glibc wrapper prior to 2.28.
 */
static int bfs_statx(int at_fd, const char *at_path, int at_flags, unsigned int mask, struct statx *buf) {
#if HAVE_STATX
	return statx(at_fd, at_path, at_flags, mask, buf);
#else
	return syscall(__NR_statx, at_fd, at_path, at_flags, mask, buf);
#endif
}

/**
 * bfs_stat() implementation backed by statx().
 */
static int bfs_statx_impl(int at_fd, const char *at_path, int at_flags, enum bfs_stat_flag flags, struct bfs_stat *buf) {
	unsigned int mask = STATX_BASIC_STATS | STATX_BTIME;
	struct statx xbuf;
	int ret = bfs_statx(at_fd, at_path, at_flags, mask, &xbuf);

	if (bfs_stat_retry(ret, at_flags, flags)) {
		at_flags |= AT_SYMLINK_NOFOLLOW;
		ret = bfs_statx(at_fd, at_path, at_flags, mask, &xbuf);
	}

	if (ret != 0) {
		return ret;
	}

	if ((xbuf.stx_mask & STATX_BASIC_STATS) != STATX_BASIC_STATS) {
		// Callers shouldn't have to check anything except BFS_STAT_BTIME
		errno = EINVAL;
		return -1;
	}

	buf->mask = 0;

	buf->dev = makedev(xbuf.stx_dev_major, xbuf.stx_dev_minor);
	buf->mask |= BFS_STAT_DEV;

	if (xbuf.stx_mask & STATX_INO) {
		buf->ino = xbuf.stx_ino;
		buf->mask |= BFS_STAT_INO;
	}

	buf->mode = xbuf.stx_mode;
	if (xbuf.stx_mask & STATX_TYPE) {
		buf->mask |= BFS_STAT_TYPE;
	}
	if (xbuf.stx_mask & STATX_MODE) {
		buf->mask |= BFS_STAT_MODE;
	}

	if (xbuf.stx_mask & STATX_NLINK) {
		buf->nlink = xbuf.stx_nlink;
		buf->mask |= BFS_STAT_NLINK;
	}

	if (xbuf.stx_mask & STATX_GID) {
		buf->gid = xbuf.stx_gid;
		buf->mask |= BFS_STAT_GID;
	}

	if (xbuf.stx_mask & STATX_UID) {
		buf->uid = xbuf.stx_uid;
		buf->mask |= BFS_STAT_UID;
	}

	if (xbuf.stx_mask & STATX_SIZE) {
		buf->size = xbuf.stx_size;
		buf->mask |= BFS_STAT_SIZE;
	}

	if (xbuf.stx_mask & STATX_BLOCKS) {
		buf->blocks = xbuf.stx_blocks;
		buf->mask |= BFS_STAT_BLOCKS;
	}

	if (xbuf.stx_mask & STATX_ATIME) {
		buf->atime.tv_sec = xbuf.stx_atime.tv_sec;
		buf->atime.tv_nsec = xbuf.stx_atime.tv_nsec;
		buf->mask |= BFS_STAT_ATIME;
	}

	if (xbuf.stx_mask & STATX_BTIME) {
		buf->btime.tv_sec = xbuf.stx_btime.tv_sec;
		buf->btime.tv_nsec = xbuf.stx_btime.tv_nsec;
		buf->mask |= BFS_STAT_BTIME;
	}

	if (xbuf.stx_mask & STATX_CTIME) {
		buf->ctime.tv_sec = xbuf.stx_ctime.tv_sec;
		buf->ctime.tv_nsec = xbuf.stx_ctime.tv_nsec;
		buf->mask |= BFS_STAT_CTIME;
	}

	if (xbuf.stx_mask & STATX_MTIME) {
		buf->mtime.tv_sec = xbuf.stx_mtime.tv_sec;
		buf->mtime.tv_nsec = xbuf.stx_mtime.tv_nsec;
		buf->mask |= BFS_STAT_MTIME;
	}

	return ret;
}

#endif // HAVE_BFS_STATX

int bfs_stat(int at_fd, const char *at_path, int at_flags, enum bfs_stat_flag flags, struct bfs_stat *buf) {
#if HAVE_BFS_STATX
	static bool has_statx = true;

	if (has_statx) {
		int ret = bfs_statx_impl(at_fd, at_path, at_flags, flags, buf);
		if (ret != 0 && errno == ENOSYS) {
			has_statx = false;
		} else {
			return ret;
		}
	}
#endif

	return bfs_stat_impl(at_fd, at_path, at_flags, flags, buf);
}

int bfs_fstat(int fd, struct bfs_stat *buf) {
#ifdef AT_EMPTY_PATH
	static bool has_at_ep = true;

	if (has_at_ep) {
		int ret = bfs_stat(fd, "", AT_EMPTY_PATH, 0, buf);
		if (ret != 0 && errno == EINVAL) {
			has_at_ep = false;
		} else {
			return ret;
		}
	}
#endif

	struct stat statbuf;
	int ret = fstat(fd, &statbuf);
	if (ret == 0) {
		bfs_stat_convert(&statbuf, buf);
	}
	return ret;
}
