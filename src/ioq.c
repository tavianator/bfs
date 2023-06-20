// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include "ioq.h"
#include "alloc.h"
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
	/** Directory allocation. */
	struct bfs_dir *dir;
	/** Base file descriptor for openat(). */
	int dfd;
	/** Path to open, relative to dfd. */
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
	pthread_cond_t cond;
};

/** Initialize an ioq_monitor. */
static int ioq_monitor_init(struct ioq_monitor *monitor) {
	if (mutex_init(&monitor->mutex, NULL) != 0) {
		return -1;
	}

	if (cond_init(&monitor->cond, NULL) != 0) {
		mutex_destroy(&monitor->mutex);
		return -1;
	}

	return 0;
}

/** Destroy an ioq_monitor. */
static void ioq_monitor_destroy(struct ioq_monitor *monitor) {
	cond_destroy(&monitor->cond);
	mutex_destroy(&monitor->mutex);
}

/**
 * An MPMC queue of I/O commands.
 */
struct ioqq {
	/** Circular buffer index mask. */
	size_t slot_mask;

	/** Monitor index mask. */
	size_t monitor_mask;
	/** Array of monitors used by the slots. */
	struct ioq_monitor *monitors;

	/** Index of next writer. */
	cache_align atomic size_t head;
	/** Index of next reader. */
	cache_align atomic size_t tail;

	/** The circular buffer itself. */
	cache_align atomic uintptr_t slots[];
};

// If we assign slots sequentially, threads will likely be operating on
// consecutive slots.  If these slots are in the same cache line, that will
// result in false sharing.  We can mitigate this by assigning slots with a
// stride larger than a cache line e.g. 0, 9, 18, ..., 1, 10, 19, ...
// As long as the stride is relatively prime to circular buffer length, we'll
// still use every available slot.  Since the length is a power of two, that
// means the stride must be odd.

#define IOQ_STRIDE ((FALSE_SHARING_SIZE / sizeof(atomic uintptr_t)) | 1)
bfs_static_assert(IOQ_STRIDE % 2 == 1);

/** Slot flag bit to indicate waiters. */
#define IOQ_BLOCKED ((uintptr_t)1)
bfs_static_assert(alignof(union ioq_cmd) > 1);

/** Destroy an I/O command queue. */
static void ioqq_destroy(struct ioqq *ioqq) {
	for (size_t i = 0; i < ioqq->monitor_mask + 1; ++i) {
		ioq_monitor_destroy(&ioqq->monitors[i]);
	}
	free(ioqq->monitors);
	free(ioqq);
}

/** Create an I/O command queue. */
static struct ioqq *ioqq_create(size_t size) {
	// Circular buffer size must be a power of two
	size = bit_ceil(size);

	struct ioqq *ioqq = ALLOC_FLEX(struct ioqq, slots, size);
	if (!ioqq) {
		return NULL;
	}

	ioqq->slot_mask = size - 1;
	ioqq->monitor_mask = -1;

	// Use a pool of monitors
	size_t nmonitors = size < 64 ? size : 64;
	ioqq->monitors = ALLOC_ARRAY(struct ioq_monitor, nmonitors);
	if (!ioqq->monitors) {
		ioqq_destroy(ioqq);
		return NULL;
	}

	for (size_t i = 0; i < nmonitors; ++i) {
		if (ioq_monitor_init(&ioqq->monitors[i]) != 0) {
			ioqq_destroy(ioqq);
			return NULL;
		}
		++ioqq->monitor_mask;
	}

	atomic_init(&ioqq->head, 0);
	atomic_init(&ioqq->tail, 0);

	for (size_t i = 0; i < size; ++i) {
		atomic_init(&ioqq->slots[i], 0);
	}

	return ioqq;
}

/** Atomically wait for a slot to change. */
static uintptr_t ioqq_wait(struct ioqq *ioqq, size_t i, uintptr_t value) {
	atomic uintptr_t *slot = &ioqq->slots[i & ioqq->slot_mask];

	struct ioq_monitor *monitor = &ioqq->monitors[i & ioqq->monitor_mask];
	mutex_lock(&monitor->mutex);

	uintptr_t ret;
	while ((ret = load(slot, relaxed)) == value) {
		// To avoid missed wakeups, it is important that
		// cond_broadcast() is not called right here
		cond_wait(&monitor->cond, &monitor->mutex);
	}

	mutex_unlock(&monitor->mutex);
	return ret;
}

/** Wake up any threads waiting on a slot. */
static void ioqq_wake(struct ioqq *ioqq, size_t i) {
	struct ioq_monitor *monitor = &ioqq->monitors[i & ioqq->monitor_mask];

	// The following implementation would clearly avoid the missed wakeup
	// issue mentioned above in ioqq_wait():
	//
	//     mutex_lock(&monitor->mutex);
	//     cond_broadcast(&monitor->cond);
	//     mutex_unlock(&monitor->mutex);
	//
	// As a minor optimization, we move the broadcast outside of the lock.
	// This optimization is correct, even though it leads to a seemingly-
	// useless empty critical section.

	mutex_lock(&monitor->mutex);
	mutex_unlock(&monitor->mutex);
	cond_broadcast(&monitor->cond);
}

/** Push a command onto the queue. */
static void ioqq_push(struct ioqq *ioqq, union ioq_cmd *cmd) {
	size_t i = fetch_add(&ioqq->head, IOQ_STRIDE, relaxed);
	atomic uintptr_t *slot = &ioqq->slots[i & ioqq->slot_mask];

	uintptr_t addr = (uintptr_t)cmd;
	bfs_assert(!(addr & IOQ_BLOCKED));

	uintptr_t prev = load(slot, relaxed);
	do {
		while (prev & ~IOQ_BLOCKED) {
			prev = fetch_or(slot, IOQ_BLOCKED, relaxed);
			if (prev & ~IOQ_BLOCKED) {
				prev = ioqq_wait(ioqq, i, prev | IOQ_BLOCKED);
			}
		}
	} while (!compare_exchange_weak(slot, &prev, addr, release, relaxed));

	if (prev & IOQ_BLOCKED) {
		ioqq_wake(ioqq, i);
	}
}

/** Pop a command from a queue. */
static union ioq_cmd *ioqq_pop(struct ioqq *ioqq) {
	size_t i = fetch_add(&ioqq->tail, IOQ_STRIDE, relaxed);
	atomic uintptr_t *slot = &ioqq->slots[i & ioqq->slot_mask];

	uintptr_t prev = load(slot, relaxed);
	do {
		while (!(prev & ~IOQ_BLOCKED)) {
			prev = fetch_or(slot, IOQ_BLOCKED, relaxed);
			if (!(prev & ~IOQ_BLOCKED)) {
				prev = ioqq_wait(ioqq, i, IOQ_BLOCKED);
			}
		}
	} while (!compare_exchange_weak(slot, &prev, 0, acquire, relaxed));

	if (prev & IOQ_BLOCKED) {
		ioqq_wake(ioqq, i);
	}
	prev &= ~IOQ_BLOCKED;

	return (union ioq_cmd *)prev;
}

/** Pop a command from a queue if one is available. */
static union ioq_cmd *ioqq_trypop(struct ioqq *ioqq) {
	size_t i = load(&ioqq->tail, relaxed);
	atomic uintptr_t *slot = &ioqq->slots[i & ioqq->slot_mask];

	uintptr_t prev = exchange(slot, 0, acquire);

	if (prev & IOQ_BLOCKED) {
		ioqq_wake(ioqq, i);
	}
	prev &= ~IOQ_BLOCKED;

	if (prev) {
#ifdef NDEBUG
		store(&ioqq->tail, i + IOQ_STRIDE, relaxed);
#else
		size_t j = fetch_add(&ioqq->tail, IOQ_STRIDE, relaxed);
		bfs_assert(j == i, "ioqq_trypop() only supports a single consumer");
#endif
	}

	return (union ioq_cmd *)prev;
}

/** Sentinel stop command. */
static union ioq_cmd IOQ_STOP;

struct ioq {
	/** The depth of the queue. */
	size_t depth;
	/** The current size of the queue. */
	size_t size;

	/** ioq_cmd command arena. */
	struct arena cmds;

	/** Pending I/O requests. */
	struct ioqq *pending;
	/** Ready I/O responses. */
	struct ioqq *ready;

	/** The number of background threads. */
	size_t nthreads;
	/** The background threads themselves. */
	pthread_t threads[];
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
		res->dir = req.dir;
		res->error = 0;
		if (bfs_opendir(req.dir, req.dfd, req.path) == 0) {
			bfs_polldir(res->dir);
		} else {
			res->error = errno;
		}

		ioqq_push(ioq->ready, cmd);
	}

	return NULL;
}

struct ioq *ioq_create(size_t depth, size_t nthreads) {
	struct ioq *ioq = ZALLOC_FLEX(struct ioq, threads, nthreads);
	if (!ioq) {
		goto fail;
	}

	ioq->depth = depth;
	ARENA_INIT(&ioq->cmds, union ioq_cmd);

	ioq->pending = ioqq_create(depth);
	if (!ioq->pending) {
		goto fail;
	}

	ioq->ready = ioqq_create(depth);
	if (!ioq->ready) {
		goto fail;
	}

	for (size_t i = 0; i < nthreads; ++i) {
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

int ioq_opendir(struct ioq *ioq, struct bfs_dir *dir, int dfd, const char *path, void *ptr) {
	if (ioq->size >= ioq->depth) {
		return -1;
	}

	union ioq_cmd *cmd = arena_alloc(&ioq->cmds);
	if (!cmd) {
		return -1;
	}

	struct ioq_req *req = &cmd->req;
	req->dir = dir;
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
	arena_free(&ioq->cmds, (union ioq_cmd *)res);
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

	ioqq_destroy(ioq->ready);
	ioqq_destroy(ioq->pending);

	arena_destroy(&ioq->cmds);

	free(ioq);
}
