// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

/**
 * An asynchronous I/O queue implementation.
 *
 * struct ioq is composed of two separate queues:
 *
 *     struct ioqq *pending; // Pending I/O requests
 *     struct ioqq *ready;   // Ready I/O responses
 *
 * Worker threads pop requests from `pending`, execute them, and push them back
 * to the `ready` queue.  The main thread pushes requests to `pending` and pops
 * them from `ready`.
 *
 * struct ioqq is a blocking MPMC queue (though it could be SPMC/MPSC for
 * pending/ready respectively).  It is implemented as a circular buffer:
 *
 *     size_t mask;            // (1 << N) - 1
 *     [padding]
 *     size_t head;            // Writer index
 *     [padding]
 *     size_t tail;            // Reader index
 *     [padding]
 *     ioq_slot slots[1 << N]; // Queue contents
 *
 * Pushes are implemented with an unconditional
 *
 *     fetch_add(&ioqq->head, IOQ_STRIDE)
 *
 * which scales better on many architectures than compare-and-swap (see [1] for
 * details).  Pops are implemented similarly.  We add IOQ_STRIDE rather than 1
 * so that successive queue elements are on different cache lines, but the
 * exposition below uses 1 for simplicity.
 *
 * Since the fetch-and-adds are unconditional, non-blocking readers can get
 * ahead of writers:
 *
 *     Reader              Writer
 *     ────────────────    ──────────────────────
 *     head:     0 → 1
 *     slots[0]: empty
 *                         tail:     0     → 1
 *                         slots[0]: empty → full
 *     head:     1 → 2
 *     slots[1]: empty!
 *
 * To avoid this, non-blocking reads (ioqq_pop(ioqq, false)) must mark the slots
 * somehow so that writers can skip them:
 *
 *     Reader                     Writer
 *     ───────────────────────    ───────────────────────
 *     head:         0 → 1
 *     slots[0]: empty → skip
 *                                tail:         0 → 1
 *                                slots[0]:  skip → empty
 *                                tail:         1 → 2
 *                                slots[1]: empty → full
 *     head:         1 → 2
 *     slots[1]:  full → empty
 *
 * As well, a reader might "lap" a writer (or another reader), so slots need to
 * count how many times they should be skipped:
 *
 *     Reader                        Writer
 *     ──────────────────────────    ─────────────────────────
 *     head:          0 → 1
 *     slots[0]:  empty → skip(1)
 *     head:          1 → 2
 *     slots[1]:  empty → skip(1)
 *     ...
 *     head:          M → 0
 *     slots[M]:  empty → skip(1)
 *     head:          0 → 1
 *     slots[0]: skip(1 → 2)
 *                                   tail:           0 → 1
 *                                   slots[0]:  skip(2 → 1)
 *                                   tail:           1 → 2
 *                                   slots[1]: skip(1) → empty
 *                                   ...
 *                                   tail:           M → 0
 *                                   slots[M]: skip(1) → empty
 *                                   tail:           0 → 1
 *                                   slots[0]: skip(1) → empty
 *                                   tail:           1 → 2
 *                                   slots[1]:   empty → full
 *     head:          1 → 2
 *     slots[1]:   full → empty
 *
 * As described in [1], this approach is susceptible to livelock if readers stay
 * ahead of writers.  This is okay for us because we don't retry failed non-
 * blocking reads.
 *
 * The slot representation uses tag bits to hold either a pointer or skip(N):
 *
 *     IOQ_SKIP (highest bit)    IOQ_BLOCKED (lowest bit)
 *        ↓                         ↓
 *        0 0 0       ...       0 0 0
 *          └──────────┬──────────┘
 *                     │
 *                value bits
 *
 * If IOQ_SKIP is unset, the value bits hold a pointer (or zero/NULL for empty).
 * If IOQ_SKIP is set, the value bits hold a negative skip count.  Writers can
 * reduce the skip count by adding 1 to the value bits, and when the count hits
 * zero, the carry will automatically clear IOQ_SKIP:
 *
 *     IOQ_SKIP                  IOQ_BLOCKED
 *        ↓                         ↓
 *        1 1 1       ...       1 0 0    skip(2)
 *        1 1 1       ...       1 1 0    skip(1)
 *        0 0 0       ...       0 0 0    empty
 *
 * The IOQ_BLOCKED flag is used to track sleeping waiters, futex-style.  To wait
 * for a slot to change, waiters call ioq_slot_wait() which sets IOQ_BLOCKED and
 * goes to sleep.  Whenever a slot is updated, if the old value had IOQ_BLOCKED
 * set, ioq_slot_wake() must be called to wake up that waiter.
 *
 * Blocking/waking uses a pool of monitors (mutex, condition variable pairs).
 * Slots are assigned round-robin to a monitor from the pool.
 *
 * [1]: https://arxiv.org/abs/2201.02179
 */

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

/** Someone might be waiting on this slot. */
#define IOQ_BLOCKED ((uintptr_t)1)
/** The next push(es) should skip this slot. */
#define IOQ_SKIP ((uintptr_t)1 << (UINTPTR_WIDTH - 1))
/** Amount to add for an additional skip. */
#define IOQ_SKIP_ONE (~IOQ_BLOCKED)

// Need room for two flag bits
bfs_static_assert(alignof(struct ioq_ent) > 2);

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
attr_noinline
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
attr_noinline
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

/** Push an entry into a slot. */
static bool ioq_slot_push(struct ioqq *ioqq, ioq_slot *slot, struct ioq_ent *ent) {
	uintptr_t prev = load(slot, relaxed);
	while (true) {
		uintptr_t next;
		if (prev & IOQ_SKIP) {
			// skip(1) → empty
			// skip(n) → skip(n - 1)
			next = (prev - IOQ_SKIP_ONE) & ~IOQ_BLOCKED;
		} else if (prev > IOQ_BLOCKED) {
			// full(ptr) → wait
			prev = ioq_slot_wait(ioqq, slot, prev);
			continue;
		} else {
			// empty → full(ptr)
			next = (uintptr_t)ent >> 1;
		}

		if (compare_exchange_weak(slot, &prev, next, release, relaxed)) {
			break;
		}
	}

	if (prev & IOQ_BLOCKED) {
		ioq_slot_wake(ioqq, slot);
	}

	return !(prev & IOQ_SKIP);
}

/** Push an entry onto the queue. */
static void ioqq_push(struct ioqq *ioqq, struct ioq_ent *ent) {
	while (true) {
		size_t i = fetch_add(&ioqq->head, IOQ_STRIDE, relaxed);
		ioq_slot *slot = &ioqq->slots[i & ioqq->slot_mask];
		if (ioq_slot_push(ioqq, slot, ent)) {
			break;
		}
	}
}

/** (Try to) pop an entry from a slot. */
static struct ioq_ent *ioq_slot_pop(struct ioqq *ioqq, ioq_slot *slot, bool block) {
	uintptr_t prev = load(slot, relaxed);
	while (true) {
		// empty     → skip(1)
		// skip(n)   → skip(n + 1)
		// full(ptr) → full(ptr - 1)
		uintptr_t next = prev + IOQ_SKIP_ONE;
		// skip(n)   → ~IOQ_BLOCKED
		// full(ptr) → 0
		next &= (next & IOQ_SKIP) ? ~IOQ_BLOCKED : 0;

		if (block && next) {
			prev = ioq_slot_wait(ioqq, slot, prev);
			continue;
		}

		if (compare_exchange_weak(slot, &prev, next, acquire, relaxed)) {
			break;
		}
	}

	if (prev & IOQ_BLOCKED) {
		ioq_slot_wake(ioqq, slot);
	}

	// empty     → 0
	// skip(n)   → 0
	// full(ptr) → ptr
	prev &= (prev & IOQ_SKIP) ? 0 : ~IOQ_BLOCKED;
	return (struct ioq_ent *)(prev << 1);
}

/** Pop an entry from the queue. */
static struct ioq_ent *ioqq_pop(struct ioqq *ioqq, bool block) {
	size_t i = fetch_add(&ioqq->tail, IOQ_STRIDE, relaxed);
	ioq_slot *slot = &ioqq->slots[i & ioqq->slot_mask];
	return ioq_slot_pop(ioqq, slot, block);
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
		ret = bfs_opendir(ent->opendir.dir, ent->opendir.dfd, ent->opendir.path, ent->opendir.flags);
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

	// Block if we have nothing else to do
	bool block = !state->prepped && !state->submitted;
	struct ioq_ent *ret = ioqq_pop(state->ioq->pending, block);

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

			ent->ret = bfs_opendir(ent->opendir.dir, fd, NULL, ent->opendir.flags);
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
		struct ioq_ent *ent = ioqq_pop(ioq->pending, true);
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

int ioq_opendir(struct ioq *ioq, struct bfs_dir *dir, int dfd, const char *path, enum bfs_dir_flags flags, void *ptr) {
	struct ioq_ent *ent = ioq_request(ioq, IOQ_OPENDIR, ptr);
	if (!ent) {
		return -1;
	}

	struct ioq_opendir *args = &ent->opendir;
	args->dir = dir;
	args->dfd = dfd;
	args->path = path;
	args->flags = flags;

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

struct ioq_ent *ioq_pop(struct ioq *ioq, bool block) {
	if (ioq->size == 0) {
		return NULL;
	}

	return ioqq_pop(ioq->ready, block);
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
