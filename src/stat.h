// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

/**
 * A facade over the stat() API that unifies some details that diverge between
 * implementations, like the names of the timespec fields and the presence of
 * file "birth" times.  On new enough Linux kernels, the facade is backed by
 * statx() instead, and so it exposes a similar interface with a mask for which
 * fields were successfully returned.
 */

#ifndef BFS_STAT_H
#define BFS_STAT_H

#include "config.h"
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#if defined(STATX_BASIC_STATS) && (!__ANDROID__ || __ANDROID_API__ >= 30)
#  define BFS_HAS_LIBC_STATX true
#elif __linux__
#  include <linux/stat.h>
#endif

#ifndef BFS_USE_STATX
#  ifdef STATX_BASIC_STATS
#    define BFS_USE_STATX true
#  endif
#endif

#if BFS_USE_SYS_PARAM_H
#  include <sys/param.h>
#endif

#ifdef DEV_BSIZE
#  define BFS_STAT_BLKSIZE DEV_BSIZE
#elif defined(S_BLKSIZE)
#  define BFS_STAT_BLKSIZE S_BLKSIZE
#else
#  define BFS_STAT_BLKSIZE 512
#endif

/**
 * bfs_stat field bitmask.
 */
enum bfs_stat_field {
	BFS_STAT_MODE   = 1 << 0,
	BFS_STAT_DEV    = 1 << 1,
	BFS_STAT_INO    = 1 << 2,
	BFS_STAT_NLINK  = 1 << 3,
	BFS_STAT_GID    = 1 << 4,
	BFS_STAT_UID    = 1 << 5,
	BFS_STAT_SIZE   = 1 << 6,
	BFS_STAT_BLOCKS = 1 << 7,
	BFS_STAT_RDEV   = 1 << 8,
	BFS_STAT_ATTRS  = 1 << 9,
	BFS_STAT_ATIME  = 1 << 10,
	BFS_STAT_BTIME  = 1 << 11,
	BFS_STAT_CTIME  = 1 << 12,
	BFS_STAT_MTIME  = 1 << 13,
};

/**
 * Get the human-readable name of a bfs_stat field.
 */
const char *bfs_stat_field_name(enum bfs_stat_field field);

/**
 * bfs_stat() flags.
 */
enum bfs_stat_flags {
	/** Follow symlinks (the default). */
	BFS_STAT_FOLLOW = 0,
	/** Never follow symlinks. */
	BFS_STAT_NOFOLLOW = 1 << 0,
	/** Try to follow symlinks, but fall back to the link itself if broken. */
	BFS_STAT_TRYFOLLOW = 1 << 1,
	/** Try to use cached values without synchronizing remote filesystems. */
	BFS_STAT_NOSYNC = 1 << 2,
};

/**
 * Facade over struct stat.
 */
struct bfs_stat {
	/** Bitmask indicating filled fields. */
	enum bfs_stat_field mask;

	/** File type and access mode. */
	mode_t mode;
	/** Device ID containing the file. */
	dev_t dev;
	/** Inode number. */
	ino_t ino;
	/** Number of hard links. */
	nlink_t nlink;
	/** Owner group ID. */
	gid_t gid;
	/** Owner user ID. */
	uid_t uid;
	/** File size in bytes. */
	off_t size;
	/** Number of disk blocks allocated (of size BFS_STAT_BLKSIZE). */
	blkcnt_t blocks;
	/** The device ID represented by this file. */
	dev_t rdev;

	/** Attributes/flags set on the file. */
	unsigned long long attrs;

	/** Access time. */
	struct timespec atime;
	/** Birth/creation time. */
	struct timespec btime;
	/** Status change time. */
	struct timespec ctime;
	/** Modification time. */
	struct timespec mtime;
};

/**
 * Facade over fstatat().
 *
 * @param at_fd
 *         The base file descriptor for the lookup.
 * @param at_path
 *         The path to stat, relative to at_fd.  Pass NULL to fstat() at_fd
 *         itself.
 * @param flags
 *         Flags that affect the lookup.
 * @param[out] buf
 *         A place to store the stat buffer, if successful.
 * @return
 *         0 on success, -1 on error.
 */
int bfs_stat(int at_fd, const char *at_path, enum bfs_stat_flags flags, struct bfs_stat *buf);

/**
 * Convert bfs_stat_flags to fstatat() flags.
 */
int bfs_fstatat_flags(enum bfs_stat_flags flags);

/**
 * Convert struct stat to struct bfs_stat.
 */
void bfs_stat_convert(struct bfs_stat *dest, const struct stat *src);

#if BFS_USE_STATX
/**
 * Convert bfs_stat_flags to statx() flags.
 */
int bfs_statx_flags(enum bfs_stat_flags flags);

/**
 * Convert struct statx to struct bfs_stat.
 */
int bfs_statx_convert(struct bfs_stat *dest, const struct statx *src);
#endif

/**
 * Get a particular time field from a bfs_stat() buffer.
 */
const struct timespec *bfs_stat_time(const struct bfs_stat *buf, enum bfs_stat_field field);

/**
 * A unique ID for a file.
 */
typedef unsigned char bfs_file_id[sizeof(dev_t) + sizeof(ino_t)];

/**
 * Compute a unique ID for a file.
 */
void bfs_stat_id(const struct bfs_stat *buf, bfs_file_id *id);

#endif // BFS_STAT_H
