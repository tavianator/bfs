// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

/**
 * Asynchronous I/O queues.
 */

#ifndef BFS_IOQ_H
#define BFS_IOQ_H

#include <stddef.h>

/**
 * An queue of asynchronous I/O operations.
 */
struct ioq;

/**
 * An I/O queue response.
 */
struct ioq_res {
	/** The opened directory. */
	struct bfs_dir *dir;
	/** The error code, if the operation failed. */
	int error;

	/** Arbitrary user data. */
	void *ptr;
};

/**
 * Create an I/O queue.
 *
 * @param depth
 *         The maximum depth of the queue.
 * @param nthreads
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
 * Asynchronous bfs_opendir().
 *
 * @param ioq
 *         The I/O queue.
 * @param dir
 *         The allocated directory.
 * @param dfd
 *         The base file descriptor.
 * @param path
 *         The path to open, relative to dfd.
 * @param ptr
 *         An arbitrary pointer to associate with the request.
 * @return
 *         0 on success, or -1 on failure.
 */
int ioq_opendir(struct ioq *ioq, struct bfs_dir *dir, int dfd, const char *path, void *ptr);

/**
 * Pop a response from the queue.
 *
 * @param ioq
 *         The I/O queue.
 * @return
 *         The next response, or NULL.
 */
struct ioq_res *ioq_pop(struct ioq *ioq);

/**
 * Pop a response from the queue, without blocking.
 *
 * @param ioq
 *         The I/O queue.
 * @return
 *         The next response, or NULL.
 */
struct ioq_res *ioq_trypop(struct ioq *ioq);

/**
 * Free a response.
 *
 * @param ioq
 *         The I/O queue.
 * @param res
 *         The response to free.
 */
void ioq_free(struct ioq *ioq, struct ioq_res *res);

/**
 * Cancel any pending I/O operations.
 */
void ioq_cancel(struct ioq *ioq);

/**
 * Stop and destroy an I/O queue.
 */
void ioq_destroy(struct ioq *ioq);

#endif // BFS_IOQ_H
