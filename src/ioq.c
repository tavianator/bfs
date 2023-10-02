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
#include "sanity.h"
#include "thread.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>

#if BFS_USE_LIBURING
#  include <liburing.h>
#endif

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

/** A single entry in a command queue. */
typedef atomic uintptr_t ioq_slot;

/** Slot flag bit to indicate waiters. */
#define IOQ_BLOCKED ((uintptr_t)1)
bfs_static_assert(alignof(struct ioq_ent) > 1);

/** Check if a slot has waiters. */
static bool ioq_slot_blocked(uintptr_t value) {
	return value & IOQ_BLOCKED;
}

/** Extract the pointer from a slot. */
static struct ioq_ent *ioq_slot_ptr(uintptr_t value) {
	return (struct ioq_ent *)(value & ~IOQ_BLOCKED);
}

/** Check if a slot is empty. */
static bool ioq_slot_empty(uintptr_t value) {
	return !ioq_slot_ptr(value);
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
	cache_align ioq_slot slots[];
};

// If we assign slots sequentially, threads will likely be operating on
// consecutive slots.  If these slots are in the same cache line, that will
// result in false sharing.  We can mitigate this by assigning slots with a
// stride larger than a cache line e.g. 0, 9, 18, ..., 1, 10, 19, ...
// As long as the stride is relatively prime to circular buffer length, we'll
// still use every available slot.  Since the length is a power of two, that
// means the stride must be odd.

#define IOQ_STRIDE ((FALSE_SHARING_SIZE / sizeof(ioq_slot)) | 1)
bfs_static_assert(IOQ_STRIDE % 2 == 1);

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

/** Get the monitor associated with a slot. */
static struct ioq_monitor *ioq_slot_monitor(struct ioqq *ioqq, ioq_slot *slot) {
	size_t i = slot - ioqq->slots;
	return &ioqq->monitors[i & ioqq->monitor_mask];
}

/** Atomically wait for a slot to change. */
static uintptr_t ioq_slot_wait(struct ioqq *ioqq, ioq_slot *slot, uintptr_t value) {
	struct ioq_monitor *monitor = ioq_slot_monitor(ioqq, slot);
	mutex_lock(&monitor->mutex);

	uintptr_t ret = load(slot, relaxed);
	if (ret != value) {
		goto done;
	}

	if (!(value & IOQ_BLOCKED)) {
		value |= IOQ_BLOCKED;
		if (!compare_exchange_strong(slot, &ret, value, relaxed, relaxed)) {
			goto done;
		}
	}

	do {
		// To avoid missed wakeups, it is important that
		// cond_broadcast() is not called right here
		cond_wait(&monitor->cond, &monitor->mutex);
		ret = load(slot, relaxed);
	} while (ret == value);

done:
	mutex_unlock(&monitor->mutex);
	return ret;
}

/** Wake up any threads waiting on a slot. */
static void ioq_slot_wake(struct ioqq *ioqq, ioq_slot *slot) {
	struct ioq_monitor *monitor = ioq_slot_monitor(ioqq, slot);

	// The following implementation would clearly avoid the missed wakeup
	// issue mentioned above in ioq_slot_wait():
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

/** Get the next slot for writing. */
static ioq_slot *ioqq_write(struct ioqq *ioqq) {
	size_t i = fetch_add(&ioqq->head, IOQ_STRIDE, relaxed);
	return &ioqq->slots[i & ioqq->slot_mask];
}

/** Push an entry into a slot. */
static void ioq_slot_push(struct ioqq *ioqq, ioq_slot *slot, struct ioq_ent *ent) {
	uintptr_t addr = (uintptr_t)ent;
	bfs_assert(!ioq_slot_blocked(addr));

	uintptr_t prev = load(slot, relaxed);
	do {
		while (!ioq_slot_empty(prev)) {
			prev = ioq_slot_wait(ioqq, slot, prev);
		}
	} while (!compare_exchange_weak(slot, &prev, addr, release, relaxed));

	if (ioq_slot_blocked(prev)) {
		ioq_slot_wake(ioqq, slot);
	}
}

/** Push an entry onto the queue. */
static void ioqq_push(struct ioqq *ioqq, struct ioq_ent *ent) {
	ioq_slot *slot = ioqq_write(ioqq);
	ioq_slot_push(ioqq, slot, ent);
}

/** Get the next slot for reading. */
static ioq_slot *ioqq_read(struct ioqq *ioqq) {
	size_t i = fetch_add(&ioqq->tail, IOQ_STRIDE, relaxed);
	return &ioqq->slots[i & ioqq->slot_mask];
}

/** (Try to) pop an entry from a slot. */
static struct ioq_ent *ioq_slot_pop(struct ioqq *ioqq, ioq_slot *slot, bool block) {
	uintptr_t prev = load(slot, relaxed);
	do {
		while (ioq_slot_empty(prev)) {
			if (block) {
				prev = ioq_slot_wait(ioqq, slot, prev);
			} else {
				return NULL;
			}
		}
	} while (!compare_exchange_weak(slot, &prev, 0, acquire, relaxed));

	if (ioq_slot_blocked(prev)) {
		ioq_slot_wake(ioqq, slot);
	}

	return ioq_slot_ptr(prev);
}

/** Pop an entry from the queue. */
static struct ioq_ent *ioqq_pop(struct ioqq *ioqq) {
	ioq_slot *slot = ioqq_read(ioqq);
	return ioq_slot_pop(ioqq, slot, true);
}

/** Pop an entry from the queue if one is available. */
static struct ioq_ent *ioqq_trypop(struct ioqq *ioqq) {
	size_t i = load(&ioqq->tail, relaxed);
	ioq_slot *slot = &ioqq->slots[i & ioqq->slot_mask];

	struct ioq_ent *ret = ioq_slot_pop(ioqq, slot, false);
	if (ret) {
		size_t j = exchange(&ioqq->tail, i + IOQ_STRIDE, relaxed);
		bfs_assert(j == i, "Detected multiple consumers");
		(void)j;
	}

	return ret;
}

/** Sentinel stop command. */
static struct ioq_ent IOQ_STOP;

/** I/O queue thread-specific data. */
struct ioq_thread {
	/** The thread handle. */
	pthread_t id;
	/** Pointer back to the I/O queue. */
	struct ioq *parent;

#if BFS_USE_LIBURING
	/** io_uring instance. */
	struct io_uring ring;
	/** Any error that occurred initializing the ring. */
	int ring_err;
#endif
};

struct ioq {
	/** The depth of the queue. */
	size_t depth;
	/** The current size of the queue. */
	size_t size;
	/** Cancellation flag. */
	atomic bool cancel;

	/** ioq_ent arena. */
	struct arena ents;

	/** Pending I/O requests. */
	struct ioqq *pending;
	/** Ready I/O responses. */
	struct ioqq *ready;

	/** The number of background threads. */
	size_t nthreads;
	/** The background threads themselves. */
	struct ioq_thread threads[];
};

/** Cancel a request if we need to. */
static bool ioq_check_cancel(struct ioq *ioq, struct ioq_ent *ent) {
	if (!load(&ioq->cancel, relaxed)) {
		return false;
	}

	// Always close(), even if we're cancelled, just like a real EINTR
	if (ent->op == IOQ_CLOSE || ent->op == IOQ_CLOSEDIR) {
		return false;
	}

	ent->ret = -1;
	ent->error = EINTR;
	ioqq_push(ioq->ready, ent);
	return true;
}

/** Handle a single request synchronously. */
static void ioq_handle(struct ioq *ioq, struct ioq_ent *ent) {
	int ret;

	switch (ent->op) {
	case IOQ_CLOSE:
		ret = xclose(ent->close.fd);
		break;

	case IOQ_OPENDIR:
		ret = bfs_opendir(ent->opendir.dir, ent->opendir.dfd, ent->opendir.path);
		if (ret == 0) {
			bfs_polldir(ent->opendir.dir);
		}
		break;

	case IOQ_CLOSEDIR:
		ret = bfs_closedir(ent->closedir.dir);
		break;

	default:
		bfs_bug("Unknown ioq_op %d", (int)ent->op);
		ret = -1;
		errno = ENOSYS;
		break;
	}

	ent->ret = ret;
	ent->error = ret == 0 ? 0 : errno;

	ioqq_push(ioq->ready, ent);
}

#if BFS_USE_LIBURING
/** io_uring worker state. */
struct ioq_ring_state {
	/** The I/O queue. */
	struct ioq *ioq;
	/** The io_uring. */
	struct io_uring *ring;
	/** The current ioq->pending slot. */
	ioq_slot *slot;
	/** Number of prepped, unsubmitted SQEs. */
	size_t prepped;
	/** Number of submitted, unreaped SQEs. */
	size_t submitted;
	/** Whether to stop the loop. */
	bool stop;
};

/** Pop a request for ioq_ring_prep(). */
static struct ioq_ent *ioq_ring_pop(struct ioq_ring_state *state) {
	if (state->stop) {
		return NULL;
	}

	// Advance to the next slot if necessary
	struct ioq *ioq = state->ioq;
	if (!state->slot) {
		state->slot = ioqq_read(ioq->pending);
	}

	// Block if we have nothing else to do
	bool block = !state->prepped && !state->submitted;
	struct ioq_ent *ret = ioq_slot_pop(ioq->pending, state->slot, block);

	if (ret) {
		// Got an entry, move to the next slot next time
		state->slot = NULL;
	}

	if (ret == &IOQ_STOP) {
		state->stop = true;
		ret = NULL;
	}

	return ret;
}

/** Prep a single SQE. */
static void ioq_prep_sqe(struct io_uring_sqe *sqe, struct ioq_ent *ent) {
	switch (ent->op) {
	case IOQ_CLOSE:
		io_uring_prep_close(sqe, ent->close.fd);
		break;

	case IOQ_OPENDIR:
		io_uring_prep_openat(sqe, ent->opendir.dfd, ent->opendir.path, O_RDONLY | O_CLOEXEC | O_DIRECTORY, 0);
		break;

#if BFS_USE_UNWRAPDIR
	case IOQ_CLOSEDIR:
		io_uring_prep_close(sqe, bfs_unwrapdir(ent->closedir.dir));
		break;
#endif

	default:
		bfs_bug("Unknown ioq_op %d", (int)ent->op);
		io_uring_prep_nop(sqe);
		break;
	}

	io_uring_sqe_set_data(sqe, ent);
}

/** Prep a batch of SQEs. */
static bool ioq_ring_prep(struct ioq_ring_state *state) {
	struct ioq *ioq = state->ioq;
	struct io_uring *ring = state->ring;

	while (io_uring_sq_space_left(ring)) {
		struct ioq_ent *ent = ioq_ring_pop(state);
		if (!ent) {
			break;
		}

		if (ioq_check_cancel(ioq, ent)) {
			continue;
		}

#if !BFS_USE_UNWRAPDIR
		if (ent->op == IOQ_CLOSEDIR) {
			ioq_handle(ioq, ent);
			continue;
		}
#endif

		struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
		ioq_prep_sqe(sqe, ent);
		++state->prepped;
	}

	return state->prepped || state->submitted;
}

/** Reap a batch of SQEs. */
static void ioq_ring_reap(struct ioq_ring_state *state) {
	struct ioq *ioq = state->ioq;
	struct io_uring *ring = state->ring;

	while (state->prepped) {
		int ret = io_uring_submit_and_wait(ring, 1);
		if (ret > 0) {
			state->prepped -= ret;
			state->submitted += ret;
		}
	}

	while (state->submitted) {
		struct io_uring_cqe *cqe;
		if (io_uring_wait_cqe(ring, &cqe) < 0) {
			continue;
		}

		struct ioq_ent *ent = io_uring_cqe_get_data(cqe);
		ent->ret = cqe->res >= 0 ? cqe->res : -1;
		ent->error = cqe->res < 0 ? -cqe->res : 0;
		io_uring_cqe_seen(ring, cqe);
		--state->submitted;

		if (ent->op == IOQ_OPENDIR && ent->ret >= 0) {
			int fd = ent->ret;
			if (ioq_check_cancel(ioq, ent)) {
				xclose(fd);
				continue;
			}

			ent->ret = bfs_opendir(ent->opendir.dir, fd, NULL);
			if (ent->ret == 0) {
				// TODO: io_uring_prep_getdents()
				bfs_polldir(ent->opendir.dir);
			} else {
				ent->error = errno;
			}
		}

		ioqq_push(ioq->ready, ent);
	}
}

/** io_uring worker loop. */
static void ioq_ring_work(struct ioq_thread *thread) {
	struct ioq_ring_state state = {
		.ioq = thread->parent,
		.ring = &thread->ring,
	};

	while (ioq_ring_prep(&state)) {
		ioq_ring_reap(&state);
	}
}
#endif

/** Synchronous syscall loop. */
static void ioq_sync_work(struct ioq_thread *thread) {
	struct ioq *ioq = thread->parent;

	while (true) {
		struct ioq_ent *ent = ioqq_pop(ioq->pending);
		if (ent == &IOQ_STOP) {
			break;
		}

		if (!ioq_check_cancel(ioq, ent)) {
			ioq_handle(ioq, ent);
		}
	}
}

/** Background thread entry point. */
static void *ioq_work(void *ptr) {
	struct ioq_thread *thread = ptr;

#if BFS_USE_LIBURING
	if (thread->ring_err == 0) {
		ioq_ring_work(thread);
		return NULL;
	}
#endif

	ioq_sync_work(thread);
	return NULL;
}

struct ioq *ioq_create(size_t depth, size_t nthreads) {
	struct ioq *ioq = ZALLOC_FLEX(struct ioq, threads, nthreads);
	if (!ioq) {
		goto fail;
	}

	ioq->depth = depth;
	ARENA_INIT(&ioq->ents, struct ioq_ent);

	ioq->pending = ioqq_create(depth);
	if (!ioq->pending) {
		goto fail;
	}

	ioq->ready = ioqq_create(depth);
	if (!ioq->ready) {
		goto fail;
	}

	for (size_t i = 0; i < nthreads; ++i) {
		struct ioq_thread *thread = &ioq->threads[i];
		thread->parent = ioq;

#if BFS_USE_LIBURING
		struct ioq_thread *prev = i ? &ioq->threads[i - 1] : NULL;
		if (prev && prev->ring_err) {
			thread->ring_err = prev->ring_err;
		} else {
			// Share io-wq workers between rings
			struct io_uring_params params = {0};
			if (prev) {
				params.flags |= IORING_SETUP_ATTACH_WQ;
				params.wq_fd = prev->ring.ring_fd;
			}

			size_t entries = depth / nthreads;
			if (entries < 16) {
				entries = 16;
			}
			thread->ring_err = -io_uring_queue_init_params(entries, &thread->ring, &params);
		}
#endif

		if (thread_create(&thread->id, NULL, ioq_work, thread) != 0) {
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

size_t ioq_capacity(const struct ioq *ioq) {
	return ioq->depth - ioq->size;
}

static struct ioq_ent *ioq_request(struct ioq *ioq, enum ioq_op op, void *ptr) {
	if (load(&ioq->cancel, relaxed)) {
		errno = EINTR;
		return NULL;
	}

	if (ioq->size >= ioq->depth) {
		errno = EAGAIN;
		return NULL;
	}

	struct ioq_ent *ent = arena_alloc(&ioq->ents);
	if (!ent) {
		return NULL;
	}

	ent->op = op;
	ent->ptr = ptr;
	++ioq->size;
	return ent;
}

int ioq_close(struct ioq *ioq, int fd, void *ptr) {
	struct ioq_ent *ent = ioq_request(ioq, IOQ_CLOSE, ptr);
	if (!ent) {
		return -1;
	}

	ent->close.fd = fd;

	ioqq_push(ioq->pending, ent);
	return 0;
}

int ioq_opendir(struct ioq *ioq, struct bfs_dir *dir, int dfd, const char *path, void *ptr) {
	struct ioq_ent *ent = ioq_request(ioq, IOQ_OPENDIR, ptr);
	if (!ent) {
		return -1;
	}

	struct ioq_opendir *args = &ent->opendir;
	args->dir = dir;
	args->dfd = dfd;
	args->path = path;

	ioqq_push(ioq->pending, ent);
	return 0;
}

int ioq_closedir(struct ioq *ioq, struct bfs_dir *dir, void *ptr) {
	struct ioq_ent *ent = ioq_request(ioq, IOQ_CLOSEDIR, ptr);
	if (!ent) {
		return -1;
	}

	ent->closedir.dir = dir;

	ioqq_push(ioq->pending, ent);
	return 0;
}

struct ioq_ent *ioq_pop(struct ioq *ioq) {
	if (ioq->size == 0) {
		return NULL;
	}

	return ioqq_pop(ioq->ready);
}

struct ioq_ent *ioq_trypop(struct ioq *ioq) {
	if (ioq->size == 0) {
		return NULL;
	}

	return ioqq_trypop(ioq->ready);
}

void ioq_free(struct ioq *ioq, struct ioq_ent *ent) {
	bfs_assert(ioq->size > 0);
	--ioq->size;

	arena_free(&ioq->ents, ent);
}

void ioq_cancel(struct ioq *ioq) {
	if (!exchange(&ioq->cancel, true, relaxed)) {
		for (size_t i = 0; i < ioq->nthreads; ++i) {
			ioqq_push(ioq->pending, &IOQ_STOP);
		}
	}
}

void ioq_destroy(struct ioq *ioq) {
	if (!ioq) {
		return;
	}

	ioq_cancel(ioq);

	for (size_t i = 0; i < ioq->nthreads; ++i) {
		struct ioq_thread *thread = &ioq->threads[i];
		thread_join(thread->id, NULL);
#if BFS_USE_LIBURING
		io_uring_queue_exit(&thread->ring);
#endif
	}

	ioqq_destroy(ioq->ready);
	ioqq_destroy(ioq->pending);

	arena_destroy(&ioq->ents);

	free(ioq);
}
