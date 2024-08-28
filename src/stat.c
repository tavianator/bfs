// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include "prelude.h"
#include "stat.h"

#include "atomic.h"
#include "bfs.h"
#include "bfstd.h"
#include "diag.h"
#include "sanity.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#if BFS_USE_STATX && !BFS_HAS_STATX
#  include <linux/stat.h>
#  include <sys/syscall.h>
#  include <unistd.h>
#endif

const char *bfs_stat_field_name(enum bfs_stat_field field) {
	switch (field) {
	case BFS_STAT_MODE:
		return "mode";
	case BFS_STAT_DEV:
		return "device number";
	case BFS_STAT_INO:
		return "inode nunmber";
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

	bfs_bug("Unrecognized stat field %d", (int)field);
	return "???";
}

int bfs_fstatat_flags(enum bfs_stat_flags flags) {
	int ret = 0;

	if (flags & BFS_STAT_NOFOLLOW) {
		ret |= AT_SYMLINK_NOFOLLOW;
	}

#ifdef AT_NO_AUTOMOUNT
	ret |= AT_NO_AUTOMOUNT;
#endif

	return ret;
}

void bfs_stat_convert(struct bfs_stat *dest, const struct stat *src) {
	dest->mask = 0;

	dest->mode = src->st_mode;
	dest->mask |= BFS_STAT_MODE;

	dest->dev = src->st_dev;
	dest->mask |= BFS_STAT_DEV;

	dest->ino = src->st_ino;
	dest->mask |= BFS_STAT_INO;

	dest->nlink = src->st_nlink;
	dest->mask |= BFS_STAT_NLINK;

	dest->gid = src->st_gid;
	dest->mask |= BFS_STAT_GID;

	dest->uid = src->st_uid;
	dest->mask |= BFS_STAT_UID;

	dest->size = src->st_size;
	dest->mask |= BFS_STAT_SIZE;

	dest->blocks = src->st_blocks;
	dest->mask |= BFS_STAT_BLOCKS;

	dest->rdev = src->st_rdev;
	dest->mask |= BFS_STAT_RDEV;

#if BFS_HAS_ST_FLAGS
	dest->attrs = src->st_flags;
	dest->mask |= BFS_STAT_ATTRS;
#endif

	dest->atime = ST_ATIM(*src);
	dest->mask |= BFS_STAT_ATIME;

	dest->ctime = ST_CTIM(*src);
	dest->mask |= BFS_STAT_CTIME;

	dest->mtime = ST_MTIM(*src);
	dest->mask |= BFS_STAT_MTIME;

#if BFS_HAS_ST_BIRTHTIM
	dest->btime = src->st_birthtim;
	dest->mask |= BFS_STAT_BTIME;
#elif BFS_HAS___ST_BIRTHTIM
	dest->btime = src->__st_birthtim;
	dest->mask |= BFS_STAT_BTIME;
#elif BFS_HAS_ST_BIRTHTIMESPEC
	dest->btime = src->st_birthtimespec;
	dest->mask |= BFS_STAT_BTIME;
#endif
}

/**
 * bfs_stat() implementation backed by stat().
 */
static int bfs_stat_impl(int at_fd, const char *at_path, int at_flags, struct bfs_stat *buf) {
	struct stat statbuf;
	int ret = fstatat(at_fd, at_path, &statbuf, at_flags);
	if (ret == 0) {
		bfs_stat_convert(buf, &statbuf);
	}
	return ret;
}

#if BFS_USE_STATX

/**
 * Wrapper for the statx() system call, which had no glibc wrapper prior to 2.28.
 */
static int bfs_statx(int at_fd, const char *at_path, int at_flags, unsigned int mask, struct statx *buf) {
#if BFS_HAS_STATX
	int ret = statx(at_fd, at_path, at_flags, mask, buf);
#else
	int ret = syscall(SYS_statx, at_fd, at_path, at_flags, mask, buf);
#endif

	if (ret == 0) {
		// -fsanitize=memory doesn't know about statx()
		sanitize_init(buf);
	}

	return ret;
}

int bfs_statx_flags(enum bfs_stat_flags flags) {
	int ret = bfs_fstatat_flags(flags);

	if (flags & BFS_STAT_NOSYNC) {
		ret |= AT_STATX_DONT_SYNC;
	}

	return ret;
}

int bfs_statx_convert(struct bfs_stat *dest, const struct statx *src) {
	// Callers shouldn't have to check anything except the times
	const unsigned int guaranteed = STATX_BASIC_STATS & ~(STATX_ATIME | STATX_CTIME | STATX_MTIME);
	if ((src->stx_mask & guaranteed) != guaranteed) {
		errno = ENOTSUP;
		return -1;
	}

	dest->mask = 0;

	dest->mode = src->stx_mode;
	dest->mask |= BFS_STAT_MODE;

	dest->dev = xmakedev(src->stx_dev_major, src->stx_dev_minor);
	dest->mask |= BFS_STAT_DEV;

	dest->ino = src->stx_ino;
	dest->mask |= BFS_STAT_INO;

	dest->nlink = src->stx_nlink;
	dest->mask |= BFS_STAT_NLINK;

	dest->gid = src->stx_gid;
	dest->mask |= BFS_STAT_GID;

	dest->uid = src->stx_uid;
	dest->mask |= BFS_STAT_UID;

	dest->size = src->stx_size;
	dest->mask |= BFS_STAT_SIZE;

	dest->blocks = src->stx_blocks;
	dest->mask |= BFS_STAT_BLOCKS;

	dest->rdev = xmakedev(src->stx_rdev_major, src->stx_rdev_minor);
	dest->mask |= BFS_STAT_RDEV;

	dest->attrs = src->stx_attributes;
	dest->mask |= BFS_STAT_ATTRS;

	if (src->stx_mask & STATX_ATIME) {
		dest->atime.tv_sec = src->stx_atime.tv_sec;
		dest->atime.tv_nsec = src->stx_atime.tv_nsec;
		dest->mask |= BFS_STAT_ATIME;
	}

	if (src->stx_mask & STATX_BTIME) {
		dest->btime.tv_sec = src->stx_btime.tv_sec;
		dest->btime.tv_nsec = src->stx_btime.tv_nsec;
		dest->mask |= BFS_STAT_BTIME;
	}

	if (src->stx_mask & STATX_CTIME) {
		dest->ctime.tv_sec = src->stx_ctime.tv_sec;
		dest->ctime.tv_nsec = src->stx_ctime.tv_nsec;
		dest->mask |= BFS_STAT_CTIME;
	}

	if (src->stx_mask & STATX_MTIME) {
		dest->mtime.tv_sec = src->stx_mtime.tv_sec;
		dest->mtime.tv_nsec = src->stx_mtime.tv_nsec;
		dest->mask |= BFS_STAT_MTIME;
	}

	return 0;
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

	return bfs_statx_convert(buf, &xbuf);
}

#endif // BFS_USE_STATX

/**
 * Calls the stat() implementation with explicit flags.
 */
static int bfs_stat_explicit(int at_fd, const char *at_path, int at_flags, struct bfs_stat *buf) {
#if BFS_USE_STATX
	static atomic bool has_statx = true;

	if (load(&has_statx, relaxed)) {
		int ret = bfs_statx_impl(at_fd, at_path, at_flags, buf);
		if (ret != 0 && errno_is_like(ENOSYS)) {
			store(&has_statx, false, relaxed);
		} else {
			return ret;
		}
	}

	at_flags &= ~AT_STATX_DONT_SYNC;
#endif

	return bfs_stat_impl(at_fd, at_path, at_flags, buf);
}

/**
 * Implements the BFS_STAT_TRYFOLLOW retry logic.
 */
static int bfs_stat_tryfollow(int at_fd, const char *at_path, int at_flags, enum bfs_stat_flags bfs_flags, struct bfs_stat *buf) {
	int ret = bfs_stat_explicit(at_fd, at_path, at_flags, buf);

	if (ret != 0
	    && (bfs_flags & (BFS_STAT_NOFOLLOW | BFS_STAT_TRYFOLLOW)) == BFS_STAT_TRYFOLLOW
	    && errno_is_like(ENOENT))
	{
		at_flags |= AT_SYMLINK_NOFOLLOW;
		ret = bfs_stat_explicit(at_fd, at_path, at_flags, buf);
	}

	return ret;
}

int bfs_stat(int at_fd, const char *at_path, enum bfs_stat_flags flags, struct bfs_stat *buf) {
#if BFS_USE_STATX
	int at_flags = bfs_statx_flags(flags);
#else
	int at_flags = bfs_fstatat_flags(flags);
#endif

	if (at_path) {
		return bfs_stat_tryfollow(at_fd, at_path, at_flags, flags, buf);
	}

#if BFS_USE_STATX
	// If we have statx(), use it with AT_EMPTY_PATH for its extra features
	at_flags |= AT_EMPTY_PATH;
	return bfs_stat_explicit(at_fd, "", at_flags, buf);
#else
	// Otherwise, just use fstat() rather than fstatat(at_fd, ""), to save
	// the kernel the trouble of copying in the empty string
	struct stat sb;
	if (fstat(at_fd, &sb) != 0) {
		return -1;
	}

	bfs_stat_convert(buf, &sb);
	return 0;
#endif
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
