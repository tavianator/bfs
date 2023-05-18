// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include "stat.h"
#include "bfstd.h"
#include "config.h"
#include "diag.h"
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

#if defined(STATX_BASIC_STATS) && (!__ANDROID__ || __ANDROID_API__ >= 30)
#  define BFS_LIBC_STATX true
#elif __linux__
#  include <linux/stat.h>
#  include <sys/syscall.h>
#  include <unistd.h>
#endif

#if BFS_LIBC_STATX || defined(SYS_statx)
#  define BFS_STATX true
#endif

const char *bfs_stat_field_name(enum bfs_stat_field field) {
	switch (field) {
	case BFS_STAT_DEV:
		return "device number";
	case BFS_STAT_INO:
		return "inode nunmber";
	case BFS_STAT_TYPE:
		return "type";
	case BFS_STAT_MODE:
		return "mode";
	case BFS_STAT_NLINK:
		return "link count";
	case BFS_STAT_GID:
		return "group ID";
	case BFS_STAT_UID:
		return "user ID";
	case BFS_STAT_SIZE:
		return "size";
	case BFS_STAT_BLOCKS:
		return "block count";
	case BFS_STAT_RDEV:
		return "underlying device";
	case BFS_STAT_ATTRS:
		return "attributes";
	case BFS_STAT_ATIME:
		return "access time";
	case BFS_STAT_BTIME:
		return "birth time";
	case BFS_STAT_CTIME:
		return "change time";
	case BFS_STAT_MTIME:
		return "modification time";
	}

	bfs_bug("Unrecognized stat field");
	return "???";
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

	buf->rdev = statbuf->st_rdev;
	buf->mask |= BFS_STAT_RDEV;

#if BSD
	buf->attrs = statbuf->st_flags;
	buf->mask |= BFS_STAT_ATTRS;
#endif

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
static int bfs_stat_impl(int at_fd, const char *at_path, int at_flags, struct bfs_stat *buf) {
	struct stat statbuf;
	int ret = fstatat(at_fd, at_path, &statbuf, at_flags);
	if (ret == 0) {
		bfs_stat_convert(&statbuf, buf);
	}
	return ret;
}

#if BFS_STATX

/**
 * Wrapper for the statx() system call, which had no glibc wrapper prior to 2.28.
 */
static int bfs_statx(int at_fd, const char *at_path, int at_flags, unsigned int mask, struct statx *buf) {
#if __has_feature(memory_sanitizer)
	// -fsanitize=memory doesn't know about statx(), so tell it the memory
	// got initialized
	memset(buf, 0, sizeof(*buf));
#endif

#if BFS_LIBC_STATX
	return statx(at_fd, at_path, at_flags, mask, buf);
#else
	return syscall(SYS_statx, at_fd, at_path, at_flags, mask, buf);
#endif
}

/**
 * bfs_stat() implementation backed by statx().
 */
static int bfs_statx_impl(int at_fd, const char *at_path, int at_flags, struct bfs_stat *buf) {
	unsigned int mask = STATX_BASIC_STATS | STATX_BTIME;
	struct statx xbuf;
	int ret = bfs_statx(at_fd, at_path, at_flags, mask, &xbuf);
	if (ret != 0) {
		return ret;
	}

	// Callers shouldn't have to check anything except the times
	const unsigned int guaranteed = STATX_BASIC_STATS ^ (STATX_ATIME | STATX_CTIME | STATX_MTIME);
	if ((xbuf.stx_mask & guaranteed) != guaranteed) {
		errno = ENOTSUP;
		return -1;
	}

	buf->mask = 0;

	buf->dev = xmakedev(xbuf.stx_dev_major, xbuf.stx_dev_minor);
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

	buf->rdev = xmakedev(xbuf.stx_rdev_major, xbuf.stx_rdev_minor);
	buf->mask |= BFS_STAT_RDEV;

	buf->attrs = xbuf.stx_attributes;
	buf->mask |= BFS_STAT_ATTRS;

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

#endif // BFS_STATX

/**
 * Calls the stat() implementation with explicit flags.
 */
static int bfs_stat_explicit(int at_fd, const char *at_path, int at_flags, int x_flags, struct bfs_stat *buf) {
#if BFS_STATX
	static bool has_statx = true;

	if (has_statx) {
		int ret = bfs_statx_impl(at_fd, at_path, at_flags | x_flags, buf);
		// EPERM is commonly returned in a seccomp() sandbox that does
		// not allow statx()
		if (ret != 0 && (errno == ENOSYS || errno == EPERM)) {
			has_statx = false;
		} else {
			return ret;
		}
	}
#endif

	return bfs_stat_impl(at_fd, at_path, at_flags, buf);
}

/**
 * Implements the BFS_STAT_TRYFOLLOW retry logic.
 */
static int bfs_stat_tryfollow(int at_fd, const char *at_path, int at_flags, int x_flags, enum bfs_stat_flags bfs_flags, struct bfs_stat *buf) {
	int ret = bfs_stat_explicit(at_fd, at_path, at_flags, x_flags, buf);

	if (ret != 0
	    && (bfs_flags & (BFS_STAT_NOFOLLOW | BFS_STAT_TRYFOLLOW)) == BFS_STAT_TRYFOLLOW
	    && is_nonexistence_error(errno))
	{
		at_flags |= AT_SYMLINK_NOFOLLOW;
		ret = bfs_stat_explicit(at_fd, at_path, at_flags, x_flags, buf);
	}

	return ret;
}

int bfs_stat(int at_fd, const char *at_path, enum bfs_stat_flags flags, struct bfs_stat *buf) {
	int at_flags = 0;
	if (flags & BFS_STAT_NOFOLLOW) {
		at_flags |= AT_SYMLINK_NOFOLLOW;
	}

#if defined(AT_NO_AUTOMOUNT) && (!__GNU__ || __GLIBC_PREREQ(2, 35))
	at_flags |= AT_NO_AUTOMOUNT;
#endif

	int x_flags = 0;
#ifdef AT_STATX_DONT_SYNC
	if (flags & BFS_STAT_NOSYNC) {
		x_flags |= AT_STATX_DONT_SYNC;
	}
#endif

	if (at_path) {
		return bfs_stat_tryfollow(at_fd, at_path, at_flags, x_flags, flags, buf);
	}

	// Check __GNU__ to work around https://lists.gnu.org/archive/html/bug-hurd/2021-12/msg00001.html
#if defined(AT_EMPTY_PATH) && !__GNU__
	static bool has_at_ep = true;
	if (has_at_ep) {
		at_flags |= AT_EMPTY_PATH;
		int ret = bfs_stat_explicit(at_fd, "", at_flags, x_flags, buf);
		if (ret != 0 && errno == EINVAL) {
			has_at_ep = false;
		} else {
			return ret;
		}
	}
#endif

	struct stat statbuf;
	if (fstat(at_fd, &statbuf) == 0) {
		bfs_stat_convert(&statbuf, buf);
		return 0;
	} else {
		return -1;
	}
}

const struct timespec *bfs_stat_time(const struct bfs_stat *buf, enum bfs_stat_field field) {
	if (!(buf->mask & field)) {
		errno = ENOTSUP;
		return NULL;
	}

	switch (field) {
	case BFS_STAT_ATIME:
		return &buf->atime;
	case BFS_STAT_BTIME:
		return &buf->btime;
	case BFS_STAT_CTIME:
		return &buf->ctime;
	case BFS_STAT_MTIME:
		return &buf->mtime;
	default:
		bfs_bug("Invalid stat field for time");
		errno = EINVAL;
		return NULL;
	}
}

void bfs_stat_id(const struct bfs_stat *buf, bfs_file_id *id) {
	memcpy(*id, &buf->dev, sizeof(buf->dev));
	memcpy(*id + sizeof(buf->dev), &buf->ino, sizeof(buf->ino));
}
