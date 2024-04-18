// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

/**
 * Directories and their contents.
 */

#ifndef BFS_DIR_H
#define BFS_DIR_H

#include <sys/types.h>

/**
 * Whether the implementation uses the getdents() syscall directly, rather than
 * libc's readdir().
 */
#if !defined(BFS_USE_GETDENTS) && (__linux__ || __FreeBSD__)
#  define BFS_USE_GETDENTS (BFS_HAS_GETDENTS || BFS_HAS_GETDENTS64 | BFS_HAS_GETDENTS64_SYSCALL)
#endif

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
 * Allocate space for a directory.
 *
 * @return
 *         An allocated, unopen directory, or NULL on failure.
 */
struct bfs_dir *bfs_allocdir(void);

struct arena;

/**
 * Initialize an arena for directories.
 *
 * @param arena
 *         The arena to initialize.
 */
void bfs_dir_arena(struct arena *arena);

/**
 * bfs_opendir() flags.
 */
enum bfs_dir_flags {
	/** Include whiteouts in the results. */
	BFS_DIR_WHITEOUTS = 1 << 0,
	/** @internal Start of private flags. */
	BFS_DIR_PRIVATE   = 1 << 1,
};

/**
 * Open a directory.
 *
 * @param dir
 *         The allocated directory.
 * @param at_fd
 *         The base directory for path resolution.
 * @param at_path
 *         The path of the directory to open, relative to at_fd.  Pass NULL to
 *         open at_fd itself.
 * @param flags
 *         Flags that control which directory entries are listed.
 * @return
 *         0 on success, or -1 on failure.
 */
int bfs_opendir(struct bfs_dir *dir, int at_fd, const char *at_path, enum bfs_dir_flags flags);

/**
 * Get the file descriptor for a directory.
 */
int bfs_dirfd(const struct bfs_dir *dir);

/**
 * Performs any I/O necessary for the next bfs_readdir() call.
 *
 * @param dir
 *         The directory to poll.
 * @return
 *         1 on success, 0 on EOF, or -1 on failure.
 */
int bfs_polldir(struct bfs_dir *dir);

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
 * Whether the bfs_unwrapdir() function is supported.
 */
#ifndef BFS_USE_UNWRAPDIR
#  define BFS_USE_UNWRAPDIR (BFS_USE_GETDENTS || __FreeBSD__)
#endif

#if BFS_USE_UNWRAPDIR
/**
 * Detach the file descriptor from an open directory.
 *
 * @param dir
 *         The directory to detach.
 * @return
 *         The file descriptor of the directory.
 */
int bfs_unwrapdir(struct bfs_dir *dir);
#endif

#endif // BFS_DIR_H
