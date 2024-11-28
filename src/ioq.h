// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

/**
 * Asynchronous I/O queues.
 */

#ifndef BFS_IOQ_H
#define BFS_IOQ_H

#include "dir.h"
#include "stat.h"

#include <stddef.h>

/**
 * An queue of asynchronous I/O operations.
 */
struct ioq;

/**
 * I/O queue operations.
 */
enum ioq_op {
	/** ioq_nop(). */
	IOQ_NOP,
	/** ioq_close(). */
	IOQ_CLOSE,
	/** ioq_opendir(). */
	IOQ_OPENDIR,
	/** ioq_closedir(). */
	IOQ_CLOSEDIR,
	/** ioq_stat(). */
	IOQ_STAT,
};

/**
 * ioq_nop() types.
 */
enum ioq_nop_type {
	/** A lightweight nop that avoids syscalls. */
	IOQ_NOP_LIGHT,
	/** A heavyweight nop that involves a syscall. */
	IOQ_NOP_HEAVY,
};

/**
 * The I/O queue implementation needs two tag bits in each pointer to a struct
 * ioq_ent, so we need to ensure at least 4-byte alignment.  The natural
 * alignment is enough on most architectures, but not m68k, so over-align it.
 */
#define IOQ_ENT_ALIGN alignas(4)

/**
 * An I/O queue entry.
 */
struct ioq_ent {
	/** The I/O operation. */
	IOQ_ENT_ALIGN enum ioq_op op;

	/** The return value (on success) or negative error code (on failure). */
	int result;

	/** Arbitrary user data. */
	void *ptr;

	/** Operation-specific arguments. */
	union {
		/** ioq_nop() args. */
		struct ioq_nop {
			enum ioq_nop_type type;
		} nop;
		/** ioq_close() args. */
		struct ioq_close {
			int fd;
		} close;
		/** ioq_opendir() args. */
		struct ioq_opendir {
			struct bfs_dir *dir;
			const char *path;
			int dfd;
			enum bfs_dir_flags flags;
		} opendir;
		/** ioq_closedir() args. */
		struct ioq_closedir {
			struct bfs_dir *dir;
		} closedir;
		/** ioq_stat() args. */
		struct ioq_stat {
			const char *path;
			struct bfs_stat *buf;
			void *xbuf;
			int dfd;
			enum bfs_stat_flags flags;
		} stat;
	};
};

/**
 * Create an I/O queue.
 *
 * @depth
 *         The maximum depth of the queue.
 * @nthreads
 *         The maximum number of background threads.
 * @return
 *         The new I/O queue, or NULL on failure.
 */
struct ioq *ioq_create(size_t depth, size_t nthreads);

/**
 * Check the remaining capacity of a queue.
 */
size_t ioq_capacity(const struct ioq *ioq);

/**
 * A no-op, for benchmarking.
 *
 * @ioq
 *         The I/O queue.
 * @type
 *         The type of operation to perform.
 * @ptr
 *         An arbitrary pointer to associate with the request.
 * @return
 *         0 on success, or -1 on failure.
 */
int ioq_nop(struct ioq *ioq, enum ioq_nop_type type, void *ptr);

/**
 * Asynchronous close().
 *
 * @ioq
 *         The I/O queue.
 * @fd
 *         The fd to close.
 * @ptr
 *         An arbitrary pointer to associate with the request.
 * @return
 *         0 on success, or -1 on failure.
 */
int ioq_close(struct ioq *ioq, int fd, void *ptr);

/**
 * Asynchronous bfs_opendir().
 *
 * @ioq
 *         The I/O queue.
 * @dir
 *         The allocated directory.
 * @dfd
 *         The base file descriptor.
 * @path
 *         The path to open, relative to dfd.
 * @flags
 *         Flags that control which directory entries are listed.
 * @ptr
 *         An arbitrary pointer to associate with the request.
 * @return
 *         0 on success, or -1 on failure.
 */
int ioq_opendir(struct ioq *ioq, struct bfs_dir *dir, int dfd, const char *path, enum bfs_dir_flags flags, void *ptr);

/**
 * Asynchronous bfs_closedir().
 *
 * @ioq
 *         The I/O queue.
 * @dir
 *         The directory to close.
 * @ptr
 *         An arbitrary pointer to associate with the request.
 * @return
 *         0 on success, or -1 on failure.
 */
int ioq_closedir(struct ioq *ioq, struct bfs_dir *dir, void *ptr);

/**
 * Asynchronous bfs_stat().
 *
 * @ioq
 *         The I/O queue.
 * @dfd
 *         The base file descriptor.
 * @path
 *         The path to stat, relative to dfd.
 * @flags
 *         Flags that affect the lookup.
 * @buf
 *         A place to store the stat buffer, if successful.
 * @ptr
 *         An arbitrary pointer to associate with the request.
 * @return
 *         0 on success, or -1 on failure.
 */
int ioq_stat(struct ioq *ioq, int dfd, const char *path, enum bfs_stat_flags flags, struct bfs_stat *buf, void *ptr);

/**
 * Pop a response from the queue.
 *
 * @ioq
 *         The I/O queue.
 * @return
 *         The next response, or NULL.
 */
struct ioq_ent *ioq_pop(struct ioq *ioq, bool block);

/**
 * Free a queue entry.
 *
 * @ioq
 *         The I/O queue.
 * @ent
 *         The entry to free.
 */
void ioq_free(struct ioq *ioq, struct ioq_ent *ent);

/**
 * Cancel any pending I/O operations.
 */
void ioq_cancel(struct ioq *ioq);

/**
 * Stop and destroy an I/O queue.
 */
void ioq_destroy(struct ioq *ioq);

#endif // BFS_IOQ_H
