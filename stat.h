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

#ifndef BFS_STAT_H
#define BFS_STAT_H

#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>

/**
 * bfs_stat field bitmask.
 */
enum bfs_stat_field {
	BFS_STAT_DEV    = 1 << 0,
	BFS_STAT_INO    = 1 << 1,
	BFS_STAT_TYPE   = 1 << 2,
	BFS_STAT_MODE   = 1 << 3,
	BFS_STAT_NLINK  = 1 << 4,
	BFS_STAT_GID    = 1 << 5,
	BFS_STAT_UID    = 1 << 6,
	BFS_STAT_SIZE   = 1 << 7,
	BFS_STAT_BLOCKS = 1 << 8,
	BFS_STAT_ATIME  = 1 << 9,
	BFS_STAT_BTIME  = 1 << 10,
	BFS_STAT_CTIME  = 1 << 11,
	BFS_STAT_MTIME  = 1 << 12,
};

/**
 * bfs_stat() flags.
 */
enum bfs_stat_flag {
	/** Fall back to the link itself on broken symlinks. */
	BFS_STAT_BROKEN_OK = 1 << 0,
};

#ifdef DEV_BSIZE
#	define BFS_STAT_BLKSIZE DEV_BSIZE
#elif defined(S_BLKSIZE)
#	define BFS_STAT_BLKSIZE S_BLKSIZE
#else
#	define BFS_STAT_BLKSIZE 512
#endif

/**
 * Facade over struct stat.
 */
struct bfs_stat {
	/** Bitmask indicating filled fields. */
	enum bfs_stat_field mask;

	/** Device ID containing the file. */
	dev_t dev;
	/** Inode number. */
	ino_t ino;
	/** File type and access mode. */
	mode_t mode;
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
 */
int bfs_stat(int at_fd, const char *at_path, int at_flags, enum bfs_stat_flag flags, struct bfs_stat *buf);

/**
 * Facade over fstat().
 */
int bfs_fstat(int fd, struct bfs_stat *buf);

#endif // BFS_STAT_H
