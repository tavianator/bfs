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

/**
 * Directories and their contents.
 */

#ifndef BFS_DIR_H
#define BFS_DIR_H

#include <sys/types.h>

/**
 * A directory.
 */
struct bfs_dir;

/**
 * File types.
 */
enum bfs_type {
	/** An error occurred for this file. */
	BFS_ERROR = -1,
	/** Unknown type. */
	BFS_UNKNOWN,
	/** Block device. */
	BFS_BLK,
	/** Character device. */
	BFS_CHR,
	/** Directory. */
	BFS_DIR,
	/** Solaris door. */
	BFS_DOOR,
	/** Pipe. */
	BFS_FIFO,
	/** Symbolic link. */
	BFS_LNK,
	/** Solaris event port. */
	BFS_PORT,
	/** Regular file. */
	BFS_REG,
	/** Socket. */
	BFS_SOCK,
	/** BSD whiteout. */
	BFS_WHT,
};

/**
 * Convert a bfs_stat() mode to a bfs_type.
 */
enum bfs_type bfs_mode_to_type(mode_t mode);

/**
 * A directory entry.
 */
struct bfs_dirent {
	/** The type of this file (possibly unknown). */
	enum bfs_type type;
	/** The name of this file. */
	const char *name;
};

/**
 * Open a directory.
 *
 * @param at_fd
 *         The base directory for path resolution.
 * @param at_path
 *         The path of the directory to open, relative to at_fd.  Pass NULL to
 *         open at_fd itself.
 * @return
 *         The opened directory, or NULL on failure.
 */
struct bfs_dir *bfs_opendir(int at_fd, const char *at_path);

/**
 * Get the file descriptor for a directory.
 */
int bfs_dirfd(const struct bfs_dir *dir);

/**
 * Read a directory entry.
 *
 * @param dir
 *         The directory to read.
 * @param[out] dirent
 *         The directory entry to populate.
 * @return
 *         1 on success, 0 on EOF, or -1 on failure.
 */
int bfs_readdir(struct bfs_dir *dir, struct bfs_dirent *de);

/**
 * Close a directory.
 *
 * @return
 *         0 on success, -1 on failure.
 */
int bfs_closedir(struct bfs_dir *dir);

/**
 * Free a directory, keeping an open file descriptor to it.
 *
 * @param dir
 *         The directory to free.
 * @return
 *         The file descriptor on success, or -1 on failure.
 */
int bfs_freedir(struct bfs_dir *dir);

#endif // BFS_DIR_H
