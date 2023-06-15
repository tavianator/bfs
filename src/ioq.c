// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include "ioq.h"
#include "atomic.h"
#include "bfstd.h"
#include "bit.h"
#include "config.h"
#include "diag.h"
#include "dir.h"
#include "lock.h"
#include "sanity.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>

/**
 * An I/O queue request.
 */
struct ioq_req {
	/** Base file descriptor for openat(). */
	int dfd;
	/** Relative path to dfd. */
	const char *path;

	/** Arbitrary user data. */
	void *ptr;
};

/**
 * An I/O queue command.
 */
union ioq_cmd {
	struct ioq_req req;
	struct ioq_res res;
};

/**
 * A monitor for an I/O queue slot.
 */
struct ioq_monitor {
	cache_align pthread_mutex_t mutex;
	pthread_cond_t full;
	pthread_cond_t empty;
};

/** Initialize an ioq_monitor. */
static int ioq_monitor_init(struct ioq_monitor *monitor) {
	if (mutex_init(&monitor->mutex, NULL) != 0) {
		goto fail;
	}

	if (cond_init(&monitor->full, NULL) != 0) {
		goto fail_mutex;
	}

	if (cond_init(&monitor->empty, NULL) != 0) {
		goto fail_full;
	}

	return 0;

fail_full:
	cond_destroy(&monitor->full);
fail_mutex:
	mutex_destroy(&monitor->mutex);
fail:
	return -1;
}

/** Destroy an ioq_monitor. */
static void ioq_monitor_destroy(struct ioq_monitor *monitor) {
	cond_destroy(&monitor->empty);
	cond_destroy(&monitor->full);
	mutex_destroy(&monitor->mutex);
}

/**
 * A slot in an I/O queue.
 */
struct ioq_slot {
	struct ioq_monitor *monitor;
	union ioq_cmd *cmd;
};

/** Initialize an ioq_slot. */
static void ioq_slot_init(struct ioq_slot *slot, struct ioq_monitor *monitor) {
	slot->monitor = monitor;
	slot->cmd = NULL;
}

/** Push a command into a slot. */
static void ioq_slot_push(struct ioq_slot *slot, union ioq_cmd *cmd) {
	struct ioq_monitor *monitor = slot->monitor;

	mutex_lock(&monitor->mutex);
	while (slot->cmd) {
		cond_wait(&monitor->empty, &monitor->mutex);
	}
	slot->cmd = cmd;
	mutex_unlock(&monitor->mutex);

	cond_broadcast(&monitor->full);
}

/** Pop a command from a slot. */
static union ioq_cmd *ioq_slot_pop(struct ioq_slot *slot) {
	struct ioq_monitor *monitor = slot->monitor;

	mutex_lock(&monitor->mutex);
	while (!slot->cmd) {
		cond_wait(&monitor->full, &monitor->mutex);
	}
	union ioq_cmd *ret = slot->cmd;
	slot->cmd = NULL;
	mutex_unlock(&monitor->mutex);

	cond_broadcast(&monitor->empty);

	return ret;
}

/** Pop a command from a slot, if one exists. */
static union ioq_cmd *ioq_slot_trypop(struct ioq_slot *slot) {
	struct ioq_monitor *monitor = slot->monitor;

	if (!mutex_trylock(&monitor->mutex)) {
		return NULL;
	}

	union ioq_cmd *ret = slot->cmd;
	slot->cmd = NULL;

	mutex_unlock(&monitor->mutex);

	if (ret) {
		cond_broadcast(&monitor->empty);
	}
	return ret;
}

/**
 * An MPMC queue of I/O commands.
 */
struct ioqq {
	/** Circular buffer index mask. */
	size_t mask;

	/** Number of monitors. */
	size_t nmonitors;
	/** Array of monitors used by the slots. */
	struct ioq_monitor *monitors;

	/** Index of next writer. */
	cache_align atomic size_t head;
	/** Index of next reader. */
	cache_align atomic size_t tail;

	/** The circular buffer itself. */
	cache_align struct ioq_slot slots[];
};

// If we assign slots sequentially, threads will likely be operating on
// consecutive slots.  If these slots are in the same cache line, that will
// result in false sharing.  We can mitigate this by assigning slots with a
// stride larger than a cache line e.g. 0, 9, 18, ..., 1, 10, 19, ...
// As long as the stride is relatively prime to circular buffer length, we'll
// still use every available slot.  Since the length is a power of two, that
// means the stride must be odd.

#define IOQ_STRIDE ((FALSE_SHARING_SIZE / sizeof(struct ioq_slot)) | 1)
bfs_static_assert(IOQ_STRIDE % 2 == 1);

/** Destroy an I/O command queue. */
static void ioqq_destroy(struct ioqq *ioqq) {
	for (size_t i = 0; i < ioqq->nmonitors; ++i) {
		ioq_monitor_destroy(&ioqq->monitors[i]);
	}
	free(ioqq->monitors);
	free(ioqq);
}

/** Create an I/O command queue. */
static struct ioqq *ioqq_create(size_t size) {
	// Circular buffer size must be a power of two
	size = bit_ceil(size);

	struct ioqq *ioqq = xmemalign(alignof(struct ioqq), flex_sizeof(struct ioqq, slots, size));
	if (!ioqq) {
		return NULL;
	}

	// Use a pool of monitors
	size_t nmonitors = size < 64 ? size : 64;
	ioqq->nmonitors = 0;
	ioqq->monitors = xmemalign(alignof(struct ioq_monitor), nmonitors * sizeof(struct ioq_monitor));
	if (!ioqq->monitors) {
		ioqq_destroy(ioqq);
		return NULL;
	}

	for (size_t i = 0; i < nmonitors; ++i) {
		if (ioq_monitor_init(&ioqq->monitors[i]) != 0) {
			ioqq_destroy(ioqq);
			return NULL;
		}
		++ioqq->nmonitors;
	}

	ioqq->mask = size - 1;

	atomic_init(&ioqq->head, 0);
	atomic_init(&ioqq->tail, 0);

	for (size_t i = 0; i < size; ++i) {
		ioq_slot_init(&ioqq->slots[i], &ioqq->monitors[i % nmonitors]);
	}

	return ioqq;
}

/** Push a command onto the queue. */
static void ioqq_push(struct ioqq *ioqq, union ioq_cmd *cmd) {
	size_t i = fetch_add(&ioqq->head, IOQ_STRIDE, relaxed);
	ioq_slot_push(&ioqq->slots[i & ioqq->mask], cmd);
}

/** Pop a command from a queue. */
static union ioq_cmd *ioqq_pop(struct ioqq *ioqq) {
	size_t i = fetch_add(&ioqq->tail, IOQ_STRIDE, relaxed);
	return ioq_slot_pop(&ioqq->slots[i & ioqq->mask]);
}

/** Pop a command from a queue if one is available. */
static union ioq_cmd *ioqq_trypop(struct ioqq *ioqq) {
	size_t i = load(&ioqq->tail, relaxed);
	union ioq_cmd *cmd = ioq_slot_trypop(&ioqq->slots[i & ioqq->mask]);
	if (cmd) {
#ifdef NDEBUG
		store(&ioqq->tail, i + IOQ_STRIDE, relaxed);
#else
		size_t j = fetch_add(&ioqq->tail, IOQ_STRIDE, relaxed);
		bfs_assert(j == i, "ioqq_trypop() only supports a single consumer");
#endif
	}
	return cmd;
}

/** Sentinel stop command. */
static union ioq_cmd IOQ_STOP;

struct ioq {
	/** The depth of the queue. */
	size_t depth;
	/** The current size of the queue. */
	size_t size;

	/** Pending I/O requests. */
	struct ioqq *pending;
	/** Ready I/O responses. */
	struct ioqq *ready;

	/** The number of background threads. */
	size_t nthreads;
	/** The background threads themselves. */
	pthread_t *threads;
};

/** Background thread entry point. */
static void *ioq_work(void *ptr) {
	struct ioq *ioq = ptr;

	while (true) {
		union ioq_cmd *cmd = ioqq_pop(ioq->pending);
		if (cmd == &IOQ_STOP) {
			break;
		}

		struct ioq_req req = cmd->req;
		sanitize_uninit(cmd);

		struct ioq_res *res = &cmd->res;
		res->ptr = req.ptr;
		res->dir = bfs_opendir(req.dfd, req.path);
		res->error = errno;
		if (res->dir) {
			bfs_polldir(res->dir);
		}

		ioqq_push(ioq->ready, cmd);
	}

	return NULL;
}

struct ioq *ioq_create(size_t depth, size_t threads) {
	struct ioq *ioq = malloc(sizeof(*ioq));
	if (!ioq) {
		goto fail;
	}

	ioq->depth = depth;
	ioq->size = 0;

	ioq->pending = NULL;
	ioq->ready = NULL;
	ioq->nthreads = 0;

	ioq->pending = ioqq_create(depth);
	if (!ioq->pending) {
		goto fail;
	}

	ioq->ready = ioqq_create(depth);
	if (!ioq->ready) {
		goto fail;
	}

	ioq->threads = malloc(threads * sizeof(ioq->threads[0]));
	if (!ioq->threads) {
		goto fail;
	}

	for (size_t i = 0; i < threads; ++i) {
		errno = pthread_create(&ioq->threads[i], NULL, ioq_work, ioq);
		if (errno != 0) {
			goto fail;
		}
		++ioq->nthreads;
	}

	return ioq;

	int err;
fail:
	err = errno;
	ioq_destroy(ioq);
	errno = err;
	return NULL;
}

int ioq_opendir(struct ioq *ioq, int dfd, const char *path, void *ptr) {
	if (ioq->size >= ioq->depth) {
		return -1;
	}

	union ioq_cmd *cmd = malloc(sizeof(*cmd));
	if (!cmd) {
		return -1;
	}

	struct ioq_req *req = &cmd->req;
	req->dfd = dfd;
	req->path = path;
	req->ptr = ptr;

	ioqq_push(ioq->pending, cmd);
	++ioq->size;
	return 0;
}

struct ioq_res *ioq_pop(struct ioq *ioq) {
	if (ioq->size == 0) {
		return NULL;
	}

	union ioq_cmd *cmd = ioqq_pop(ioq->ready);
	--ioq->size;
	return &cmd->res;
}

struct ioq_res *ioq_trypop(struct ioq *ioq) {
	if (ioq->size == 0) {
		return NULL;
	}

	union ioq_cmd *cmd = ioqq_trypop(ioq->ready);
	if (!cmd) {
		return NULL;
	}

	--ioq->size;
	return &cmd->res;
}

void ioq_free(struct ioq *ioq, struct ioq_res *res) {
	union ioq_cmd *cmd = (union ioq_cmd *)res;
	free(cmd);
}

void ioq_destroy(struct ioq *ioq) {
	if (!ioq) {
		return;
	}

	for (size_t i = 0; i < ioq->nthreads; ++i) {
		ioqq_push(ioq->pending, &IOQ_STOP);
	}

	for (size_t i = 0; i < ioq->nthreads; ++i) {
		if (pthread_join(ioq->threads[i], NULL) != 0) {
			abort();
		}
	}
	free(ioq->threads);

	ioqq_destroy(ioq->ready);
	ioqq_destroy(ioq->pending);

	free(ioq);
}
