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
 *     fetch_add(&ioqq->head, 1)
 *
 * which scales better on many architectures than compare-and-swap (see [1] for
 * details).  Pops are implemented similarly.  Since the fetch-and-adds are
 * unconditional, non-blocking readers can get ahead of writers:
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
#include "bfs.h"
#include "bfstd.h"
#include "bit.h"
#include "diag.h"
#include "dir.h"
#include "stat.h"
#include "thread.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#if BFS_WITH_LIBURING
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

/** Bit for IOQ_SKIP. */
#define IOQ_SKIP_BIT (UINTPTR_WIDTH - 1)
/** The next push(es) should skip this slot. */
#define IOQ_SKIP ((uintptr_t)1 << IOQ_SKIP_BIT)
/** Amount to add for an additional skip. */
#define IOQ_SKIP_ONE (~IOQ_BLOCKED)

static_assert(alignof(struct ioq_ent) >= (1 << 2), "struct ioq_ent is underaligned");

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

/** Destroy an I/O command queue. */
static void ioqq_destroy(struct ioqq *ioqq) {
	if (!ioqq) {
		return;
	}

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
	uint32_t i = slot - ioqq->slots;

	// Hash the index to de-correlate waiters
	// https://nullprogram.com/blog/2018/07/31/
	// https://github.com/skeeto/hash-prospector/issues/19#issuecomment-1120105785
	i ^= i >> 16;
	i *= UINT32_C(0x21f0aaad);
	i ^= i >> 15;
	i *= UINT32_C(0x735a2d97);
	i ^= i >> 15;

	return &ioqq->monitors[i & ioqq->monitor_mask];
}

/** Atomically wait for a slot to change. */
_noinline
static uintptr_t ioq_slot_wait(struct ioqq *ioqq, ioq_slot *slot, uintptr_t value) {
	// Try spinning a few times before blocking
	uintptr_t ret;
	for (int i = 0; i < 10; ++i) {
		// Exponential backoff
		for (int j = 0; j < (1 << i); ++j) {
			spin_loop();
		}

		// Check if the slot changed
		ret = load(slot, relaxed);
		if (ret != value) {
			return ret;
		}
	}

	// Nothing changed, start blocking
	struct ioq_monitor *monitor = ioq_slot_monitor(ioqq, slot);
	mutex_lock(&monitor->mutex);

	ret = load(slot, relaxed);
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
_noinline
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

/** Branch-free ((slot & IOQ_SKIP) ? skip : full) & ~IOQ_BLOCKED */
static uintptr_t ioq_slot_blend(uintptr_t slot, uintptr_t skip, uintptr_t full) {
	uintptr_t mask = -(slot >> IOQ_SKIP_BIT);
	uintptr_t ret = (skip & mask) | (full & ~mask);
	return ret & ~IOQ_BLOCKED;
}

/** Push an entry into a slot. */
static bool ioq_slot_push(struct ioqq *ioqq, ioq_slot *slot, struct ioq_ent *ent) {
	uintptr_t prev = load(slot, relaxed);

	while (true) {
		uintptr_t full = ioq_slot_blend(prev, 0, prev);
		if (full) {
			// full(ptr) → wait
			prev = ioq_slot_wait(ioqq, slot, prev);
			continue;
		}

		// empty   → full(ptr)
		uintptr_t next = (uintptr_t)ent >> 1;
		// skip(1) → empty
		// skip(n) → skip(n - 1)
		next = ioq_slot_blend(prev, prev - IOQ_SKIP_ONE, next);

		if (compare_exchange_weak(slot, &prev, next, release, relaxed)) {
			break;
		}
	}

	if (prev & IOQ_BLOCKED) {
		ioq_slot_wake(ioqq, slot);
	}

	return !(prev & IOQ_SKIP);
}

/** (Try to) pop an entry from a slot. */
static struct ioq_ent *ioq_slot_pop(struct ioqq *ioqq, ioq_slot *slot, bool block) {
	uintptr_t prev = load(slot, relaxed);
	while (true) {
#if __has_builtin(__builtin_prefetch)
		// Optimistically prefetch the pointer in this slot.  If this
		// slot is not full, this will prefetch an invalid address, but
		// experimentally this is worth it on both Intel (Alder Lake)
		// and AMD (Zen 2).
		__builtin_prefetch((void *)(prev << 1), 1 /* write */);
#endif

		// empty     → skip(1)
		// skip(n)   → skip(n + 1)
		// full(ptr) → full(ptr - 1)
		uintptr_t next = prev + IOQ_SKIP_ONE;
		// full(ptr) → 0
		next = ioq_slot_blend(next, next, 0);

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
	prev = ioq_slot_blend(prev, 0, prev);
	return (struct ioq_ent *)(prev << 1);
}

/** Push an entry onto the queue. */
static void ioqq_push(struct ioqq *ioqq, struct ioq_ent *ent) {
	while (true) {
		size_t i = fetch_add(&ioqq->head, 1, relaxed);
		ioq_slot *slot = &ioqq->slots[i & ioqq->slot_mask];
		if (ioq_slot_push(ioqq, slot, ent)) {
			break;
		}
	}
}

/** Push a batch of entries to the queue. */
static void ioqq_push_batch(struct ioqq *ioqq, struct ioq_ent *batch[], size_t size) {
	size_t mask = ioqq->slot_mask;
	do {
		size_t i = fetch_add(&ioqq->head, size, relaxed);
		for (size_t j = i + size; i != j; ++i) {
			ioq_slot *slot = &ioqq->slots[i & mask];
			if (ioq_slot_push(ioqq, slot, *batch)) {
				++batch;
				--size;
			}
		}
	} while (size > 0);
}

/** Pop a batch of entries from the queue. */
static void ioqq_pop_batch(struct ioqq *ioqq, struct ioq_ent *batch[], size_t size, bool block) {
	size_t mask = ioqq->slot_mask;
	size_t i = fetch_add(&ioqq->tail, size, relaxed);
	for (size_t j = i + size; i != j; ++i) {
		ioq_slot *slot = &ioqq->slots[i & mask];
		*batch++ = ioq_slot_pop(ioqq, slot, block);
		block = false;
	}
}

/** Use cache-line-sized batches. */
#define IOQ_BATCH (FALSE_SHARING_SIZE / sizeof(ioq_slot))

/**
 * A batch of I/O queue entries.
 */
struct ioq_batch {
	/** The start of the batch. */
	size_t head;
	/** The end of the batch. */
	size_t tail;
	/** The array of entries. */
	struct ioq_ent *entries[IOQ_BATCH];
};

/** Reset a batch. */
static void ioq_batch_reset(struct ioq_batch *batch) {
	batch->head = batch->tail = 0;
}

/** Check if a batch is empty. */
static bool ioq_batch_empty(const struct ioq_batch *batch) {
	return batch->head >= batch->tail;
}

/** Send a batch to a queue. */
static void ioq_batch_flush(struct ioqq *ioqq, struct ioq_batch *batch) {
	if (batch->tail > 0) {
		ioqq_push_batch(ioqq, batch->entries, batch->tail);
		ioq_batch_reset(batch);
	}
}

/** Push an entry to a batch, flushing if necessary. */
static void ioq_batch_push(struct ioqq *ioqq, struct ioq_batch *batch, struct ioq_ent *ent) {
	batch->entries[batch->tail++] = ent;

	if (batch->tail >= IOQ_BATCH) {
		ioq_batch_flush(ioqq, batch);
	}
}

/** Fill a batch from a queue. */
static bool ioq_batch_fill(struct ioqq *ioqq, struct ioq_batch *batch, bool block) {
	ioqq_pop_batch(ioqq, batch->entries, IOQ_BATCH, block);

	ioq_batch_reset(batch);
	for (size_t i = 0; i < IOQ_BATCH; ++i) {
		struct ioq_ent *ent = batch->entries[i];
		if (ent) {
			batch->entries[batch->tail++] = ent;
		}
	}

	return batch->tail > 0;
}

/** Pop an entry from a batch, filling it first if necessary. */
static struct ioq_ent *ioq_batch_pop(struct ioqq *ioqq, struct ioq_batch *batch, bool block) {
	if (ioq_batch_empty(batch)) {
		// For non-blocking pops, make sure that each ioq_batch_pop()
		// corresponds to a single (amortized) increment of ioqq->head.
		// Otherwise, we start skipping many slots and batching ends up
		// degrading performance.
		if (!block && batch->head < IOQ_BATCH) {
			++batch->head;
			return NULL;
		}

		if (!ioq_batch_fill(ioqq, batch, block)) {
			return NULL;
		}
	}

	return batch->entries[batch->head++];
}

/** Sentinel stop command. */
static struct ioq_ent IOQ_STOP;

#if BFS_WITH_LIBURING
/**
 * Supported io_uring operations.
 */
enum ioq_ring_ops {
	IOQ_RING_OPENAT = 1 << 0,
	IOQ_RING_CLOSE  = 1 << 1,
	IOQ_RING_STATX  = 1 << 2,
};
#endif

/** I/O queue thread-specific data. */
struct ioq_thread {
	/** The thread handle. */
	pthread_t id;
	/** Pointer back to the I/O queue. */
	struct ioq *parent;

#if BFS_WITH_LIBURING
	/** io_uring instance. */
	struct io_uring ring;
	/** Any error that occurred initializing the ring. */
	int ring_err;
	/** Bitmask of supported io_uring operations. */
	enum ioq_ring_ops ring_ops;
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
#if BFS_WITH_LIBURING && BFS_USE_STATX
	/** struct statx arena. */
	struct arena xbufs;
#endif

	/** Pending I/O request queue. */
	struct ioqq *pending;
	/** Ready I/O response queue. */
	struct ioqq *ready;

	/** Pending request batch. */
	struct ioq_batch pending_batch;
	/** Ready request batch. */
	struct ioq_batch ready_batch;

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

	ent->result = -EINTR;
	return true;
}

/** Dispatch a single request synchronously. */
static void ioq_dispatch_sync(struct ioq *ioq, struct ioq_ent *ent) {
	switch (ent->op) {
		case IOQ_NOP:
			if (ent->nop.type == IOQ_NOP_HEAVY) {
				// A fast, no-op syscall
				getpid();
			}
			ent->result = 0;
			return;

		case IOQ_CLOSE:
			ent->result = try(xclose(ent->close.fd));
			return;

		case IOQ_OPENDIR: {
			struct ioq_opendir *args = &ent->opendir;
			ent->result = try(bfs_opendir(args->dir, args->dfd, args->path, args->flags));
			if (ent->result >= 0) {
				bfs_polldir(args->dir);
			}
			return;
		}

		case IOQ_CLOSEDIR:
			ent->result = try(bfs_closedir(ent->closedir.dir));
			return;

		case IOQ_STAT: {
			struct ioq_stat *args = &ent->stat;
			ent->result = try(bfs_stat(args->dfd, args->path, args->flags, args->buf));
			return;
		}
	}

	bfs_bug("Unknown ioq_op %d", (int)ent->op);
	ent->result = -ENOSYS;
}

#if BFS_WITH_LIBURING

/** io_uring worker state. */
struct ioq_ring_state {
	/** The I/O queue. */
	struct ioq *ioq;
	/** The io_uring. */
	struct io_uring *ring;
	/** Supported io_uring operations. */
	enum ioq_ring_ops ops;
	/** Number of prepped, unsubmitted SQEs. */
	size_t prepped;
	/** Number of submitted, unreaped SQEs. */
	size_t submitted;
	/** Whether to stop the loop. */
	bool stop;
	/** A batch of ready entries. */
	struct ioq_batch ready;
};

/** Reap a single CQE. */
static void ioq_reap_cqe(struct ioq_ring_state *state, struct io_uring_cqe *cqe) {
	struct ioq *ioq = state->ioq;

	struct ioq_ent *ent = io_uring_cqe_get_data(cqe);
	ent->result = cqe->res;

	if (ent->result < 0) {
		goto push;
	}

	switch (ent->op) {
		case IOQ_OPENDIR: {
			int fd = ent->result;
			if (ioq_check_cancel(ioq, ent)) {
				xclose(fd);
				goto push;
			}

			struct ioq_opendir *args = &ent->opendir;
			ent->result = try(bfs_opendir(args->dir, fd, NULL, args->flags));
			if (ent->result >= 0) {
				// TODO: io_uring_prep_getdents()
				bfs_polldir(args->dir);
			} else {
				xclose(fd);
			}

			break;
		}

#if BFS_USE_STATX
		case IOQ_STAT: {
			struct ioq_stat *args = &ent->stat;
			ent->result = try(bfs_statx_convert(args->buf, args->xbuf));
			break;
		}
#endif

		default:
			break;
	}

push:
	ioq_batch_push(ioq->ready, &state->ready, ent);
}

/** Wait for submitted requests to complete. */
static void ioq_ring_drain(struct ioq_ring_state *state, size_t wait_nr) {
	struct ioq *ioq = state->ioq;
	struct io_uring *ring = state->ring;

	bfs_assert(wait_nr <= state->submitted);

	while (state->submitted > 0) {
		struct io_uring_cqe *cqe;
		if (wait_nr > 0) {
			io_uring_wait_cqes(ring, &cqe, wait_nr, NULL, NULL);
		}

		unsigned int head;
		size_t seen = 0;
		io_uring_for_each_cqe (ring, head, cqe) {
			ioq_reap_cqe(state, cqe);
			++seen;
		}

		io_uring_cq_advance(ring, seen);
		state->submitted -= seen;

		if (seen >= wait_nr) {
			break;
		}
		wait_nr -= seen;
	}

	ioq_batch_flush(ioq->ready, &state->ready);
}

/** Submit prepped SQEs, and wait for some to complete. */
static void ioq_ring_submit(struct ioq_ring_state *state) {
	struct io_uring *ring = state->ring;

	size_t unreaped = state->prepped + state->submitted;
	size_t wait_nr = 0;

	if (state->prepped == 0 && unreaped > 0) {
		// If we have no new SQEs, wait for at least one old one to
		// complete, to avoid livelock
		wait_nr = 1;
	}

	if (unreaped > ring->sq.ring_entries) {
		// Keep the completion queue below half full
		wait_nr = unreaped - ring->sq.ring_entries;
	}

	// Submit all prepped SQEs
	while (state->prepped > 0) {
		int ret = io_uring_submit_and_wait(state->ring, wait_nr);
		if (ret <= 0) {
			continue;
		}

		state->submitted += ret;
		state->prepped -= ret;
		if (state->prepped > 0) {
			// In the unlikely event of a short submission, any SQE
			// links will be broken.  Wait for all SQEs to complete
			// to preserve any ordering requirements.
			ioq_ring_drain(state, state->submitted);
			wait_nr = 0;
		}
	}

	// Drain all the CQEs we waited for (and any others that are ready)
	ioq_ring_drain(state, wait_nr);
}

/** Reserve space for a number of SQEs, submitting if necessary. */
static void ioq_reserve_sqes(struct ioq_ring_state *state, unsigned int count) {
	while (io_uring_sq_space_left(state->ring) < count) {
		ioq_ring_submit(state);
	}
}

/** Get an SQE, submitting if necessary. */
static struct io_uring_sqe *ioq_get_sqe(struct ioq_ring_state *state) {
	ioq_reserve_sqes(state, 1);
	return io_uring_get_sqe(state->ring);
}

/** Dispatch a single request asynchronously. */
static struct io_uring_sqe *ioq_dispatch_async(struct ioq_ring_state *state, struct ioq_ent *ent) {
	enum ioq_ring_ops ops = state->ops;
	struct io_uring_sqe *sqe = NULL;

	switch (ent->op) {
	case IOQ_NOP:
		if (ent->nop.type == IOQ_NOP_HEAVY) {
			sqe = ioq_get_sqe(state);
			io_uring_prep_nop(sqe);
		}
		return sqe;

	case IOQ_CLOSE:
		if (ops & IOQ_RING_CLOSE) {
			sqe = ioq_get_sqe(state);
			io_uring_prep_close(sqe, ent->close.fd);
		}
		return sqe;

	case IOQ_OPENDIR:
		if (ops & IOQ_RING_OPENAT) {
			sqe = ioq_get_sqe(state);
			struct ioq_opendir *args = &ent->opendir;
			int flags = O_RDONLY | O_CLOEXEC | O_DIRECTORY;
			io_uring_prep_openat(sqe, args->dfd, args->path, flags, 0);
		}
		return sqe;

	case IOQ_CLOSEDIR:
#if BFS_USE_UNWRAPDIR
		if (ops & IOQ_RING_CLOSE) {
			sqe = ioq_get_sqe(state);
			io_uring_prep_close(sqe, bfs_unwrapdir(ent->closedir.dir));
		}
#endif
		return sqe;

	case IOQ_STAT:
#if BFS_USE_STATX
		if (ops & IOQ_RING_STATX) {
			sqe = ioq_get_sqe(state);
			struct ioq_stat *args = &ent->stat;
			int flags = bfs_statx_flags(args->flags);
			unsigned int mask = STATX_BASIC_STATS | STATX_BTIME;
			io_uring_prep_statx(sqe, args->dfd, args->path, flags, mask, args->xbuf);
		}
#endif
		return sqe;
	}

	bfs_bug("Unknown ioq_op %d", (int)ent->op);
	return NULL;
}

/** Check if ioq_ring_reap() has work to do. */
static bool ioq_ring_empty(struct ioq_ring_state *state) {
	return !state->prepped && !state->submitted && ioq_batch_empty(&state->ready);
}

/** Prep a single SQE. */
static void ioq_prep_sqe(struct ioq_ring_state *state, struct ioq_ent *ent) {
	struct ioq *ioq = state->ioq;
	if (ioq_check_cancel(ioq, ent)) {
		ioq_batch_push(ioq->ready, &state->ready, ent);
		return;
	}

	struct io_uring_sqe *sqe = ioq_dispatch_async(state, ent);
	if (sqe) {
		io_uring_sqe_set_data(sqe, ent);
		++state->prepped;
	} else {
		ioq_dispatch_sync(ioq, ent);
		ioq_batch_push(ioq->ready, &state->ready, ent);
	}
}

/** Prep a batch of SQEs. */
static bool ioq_ring_prep(struct ioq_ring_state *state) {
	if (state->stop) {
		return false;
	}

	struct ioq *ioq = state->ioq;

	struct ioq_batch pending;
	ioq_batch_reset(&pending);

	while (true) {
		bool block = ioq_ring_empty(state);
		struct ioq_ent *ent = ioq_batch_pop(ioq->pending, &pending, block);
		if (ent == &IOQ_STOP) {
			ioqq_push(ioq->pending, ent);
			state->stop = true;
			break;
		} else if (ent) {
			ioq_prep_sqe(state, ent);
		} else {
			break;
		}
	}

	bfs_assert(ioq_batch_empty(&pending));
	return !ioq_ring_empty(state);
}

/** io_uring worker loop. */
static int ioq_ring_work(struct ioq_thread *thread) {
	struct io_uring *ring = &thread->ring;

#ifdef IORING_SETUP_R_DISABLED
	if (ring->flags & IORING_SETUP_R_DISABLED) {
		if (io_uring_enable_rings(ring) != 0) {
			return -1;
		}
	}
#endif

	struct ioq_ring_state state = {
		.ioq = thread->parent,
		.ring = ring,
		.ops = thread->ring_ops,
	};

	while (ioq_ring_prep(&state)) {
		ioq_ring_submit(&state);
	}

	ioq_ring_drain(&state, state.submitted);
	return 0;
}

#endif // BFS_WITH_LIBURING

/** Synchronous syscall loop. */
static void ioq_sync_work(struct ioq_thread *thread) {
	struct ioq *ioq = thread->parent;

	struct ioq_batch pending, ready;
	ioq_batch_reset(&pending);
	ioq_batch_reset(&ready);

	while (true) {
		if (ioq_batch_empty(&pending)) {
			ioq_batch_flush(ioq->ready, &ready);
		}

		struct ioq_ent *ent = ioq_batch_pop(ioq->pending, &pending, true);
		if (ent == &IOQ_STOP) {
			ioqq_push(ioq->pending, ent);
			break;
		}

		if (!ioq_check_cancel(ioq, ent)) {
			ioq_dispatch_sync(ioq, ent);
		}
		ioq_batch_push(ioq->ready, &ready, ent);
	}

	bfs_assert(ioq_batch_empty(&pending));
	ioq_batch_flush(ioq->ready, &ready);
}

/** Background thread entry point. */
static void *ioq_work(void *ptr) {
	struct ioq_thread *thread = ptr;

#if BFS_WITH_LIBURING
	if (thread->ring_err == 0) {
		if (ioq_ring_work(thread) == 0) {
			return NULL;
		}
	}
#endif

	ioq_sync_work(thread);
	return NULL;
}

#if BFS_WITH_LIBURING
/** Test whether some io_uring setup flags are supported. */
static bool ioq_ring_probe_flags(struct io_uring_params *params, unsigned int flags) {
	unsigned int saved = params->flags;
	params->flags |= flags;

	struct io_uring ring;
	int ret = io_uring_queue_init_params(2, &ring, params);
	if (ret == 0) {
		io_uring_queue_exit(&ring);
	}

	if (ret == -EINVAL) {
		params->flags = saved;
		return false;
	}

	return true;
}
#endif

/** Initialize io_uring thread state. */
static int ioq_ring_init(struct ioq *ioq, struct ioq_thread *thread) {
#if BFS_WITH_LIBURING
	struct ioq_thread *prev = NULL;
	if (thread > ioq->threads) {
		prev = thread - 1;
	}

	if (prev && prev->ring_err) {
		thread->ring_err = prev->ring_err;
		return -1;
	}

	struct io_uring_params params = {0};

	if (prev) {
		// Share io-wq workers between rings
		params.flags = prev->ring.flags | IORING_SETUP_ATTACH_WQ;
		params.wq_fd = prev->ring.ring_fd;
	} else {
#ifdef IORING_SETUP_SUBMIT_ALL
		// Don't abort submission just because an inline request fails
		ioq_ring_probe_flags(&params, IORING_SETUP_SUBMIT_ALL);
#endif

#ifdef IORING_SETUP_R_DISABLED
		// Don't enable the ring yet (needed for SINGLE_ISSUER)
		if (ioq_ring_probe_flags(&params, IORING_SETUP_R_DISABLED)) {
#  ifdef IORING_SETUP_SINGLE_ISSUER
			// Allow optimizations assuming only one task submits SQEs
			ioq_ring_probe_flags(&params, IORING_SETUP_SINGLE_ISSUER);
#  endif
#  ifdef IORING_SETUP_DEFER_TASKRUN
			// Don't interrupt us aggresively with completion events
			ioq_ring_probe_flags(&params, IORING_SETUP_DEFER_TASKRUN);
#  endif
		}
#endif
	}

	// Use a page for each SQE ring
	size_t entries = 4096 / sizeof(struct io_uring_sqe);
	thread->ring_err = -io_uring_queue_init_params(entries, &thread->ring, &params);
	if (thread->ring_err) {
		return -1;
	}

	if (prev) {
		// Initial setup already complete
		thread->ring_ops = prev->ring_ops;
		return 0;
	}

	// Check for supported operations
	struct io_uring_probe *probe = io_uring_get_probe_ring(&thread->ring);
	if (probe) {
		if (io_uring_opcode_supported(probe, IORING_OP_OPENAT)) {
			thread->ring_ops |= IOQ_RING_OPENAT;
		}
		if (io_uring_opcode_supported(probe, IORING_OP_CLOSE)) {
			thread->ring_ops |= IOQ_RING_CLOSE;
		}
#if BFS_USE_STATX
		if (io_uring_opcode_supported(probe, IORING_OP_STATX)) {
			thread->ring_ops |= IOQ_RING_STATX;
		}
#endif
		io_uring_free_probe(probe);
	}
	if (!thread->ring_ops) {
		io_uring_queue_exit(&thread->ring);
		thread->ring_err = ENOTSUP;
		return -1;
	}

#if BFS_HAS_IO_URING_MAX_WORKERS
	// Limit the number of io_uring workers
	unsigned int values[] = {
		ioq->nthreads, // [IO_WQ_BOUND]
		0,             // [IO_WQ_UNBOUND]
	};
	io_uring_register_iowq_max_workers(&thread->ring, values);
#endif

#endif // BFS_WITH_LIBURING

	return 0;
}

/** Destroy an io_uring. */
static void ioq_ring_exit(struct ioq_thread *thread) {
#if BFS_WITH_LIBURING
	if (thread->ring_err == 0) {
		io_uring_queue_exit(&thread->ring);
	}
#endif
}

/** Create an I/O queue thread. */
static int ioq_thread_create(struct ioq *ioq, size_t i) {
	struct ioq_thread *thread = &ioq->threads[i];
	thread->parent = ioq;

	ioq_ring_init(ioq, thread);

	if (thread_create(&thread->id, NULL, ioq_work, thread) != 0) {
		ioq_ring_exit(thread);
		return -1;
	}

	char name[16];
	if (snprintf(name, sizeof(name), "ioq-%zu", i) >= 0) {
		thread_setname(thread->id, name);
	}

	return 0;
}

/** Join an I/O queue thread. */
static void ioq_thread_join(struct ioq_thread *thread) {
	thread_join(thread->id, NULL);
	ioq_ring_exit(thread);
}

struct ioq *ioq_create(size_t depth, size_t nthreads) {
	struct ioq *ioq = ZALLOC_FLEX(struct ioq, threads, nthreads);
	if (!ioq) {
		goto fail;
	}

	ioq->depth = depth;

	ARENA_INIT(&ioq->ents, struct ioq_ent);
#if BFS_WITH_LIBURING && BFS_USE_STATX
	ARENA_INIT(&ioq->xbufs, struct statx);
#endif

	ioq->pending = ioqq_create(depth);
	if (!ioq->pending) {
		goto fail;
	}

	ioq->ready = ioqq_create(depth);
	if (!ioq->ready) {
		goto fail;
	}

	ioq->nthreads = nthreads;
	for (size_t i = 0; i < nthreads; ++i) {
		if (ioq_thread_create(ioq, i) != 0) {
			ioq->nthreads = i;
			goto fail;
		}
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

int ioq_nop(struct ioq *ioq, enum ioq_nop_type type, void *ptr) {
	struct ioq_ent *ent = ioq_request(ioq, IOQ_NOP, ptr);
	if (!ent) {
		return -1;
	}

	ent->nop.type = type;

	ioq_batch_push(ioq->pending, &ioq->pending_batch, ent);
	return 0;
}

int ioq_close(struct ioq *ioq, int fd, void *ptr) {
	struct ioq_ent *ent = ioq_request(ioq, IOQ_CLOSE, ptr);
	if (!ent) {
		return -1;
	}

	ent->close.fd = fd;

	ioq_batch_push(ioq->pending, &ioq->pending_batch, ent);
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

	ioq_batch_push(ioq->pending, &ioq->pending_batch, ent);
	return 0;
}

int ioq_closedir(struct ioq *ioq, struct bfs_dir *dir, void *ptr) {
	struct ioq_ent *ent = ioq_request(ioq, IOQ_CLOSEDIR, ptr);
	if (!ent) {
		return -1;
	}

	ent->closedir.dir = dir;

	ioq_batch_push(ioq->pending, &ioq->pending_batch, ent);
	return 0;
}

int ioq_stat(struct ioq *ioq, int dfd, const char *path, enum bfs_stat_flags flags, struct bfs_stat *buf, void *ptr) {
	struct ioq_ent *ent = ioq_request(ioq, IOQ_STAT, ptr);
	if (!ent) {
		return -1;
	}

	struct ioq_stat *args = &ent->stat;
	args->dfd = dfd;
	args->path = path;
	args->flags = flags;
	args->buf = buf;

#if BFS_WITH_LIBURING && BFS_USE_STATX
	args->xbuf = arena_alloc(&ioq->xbufs);
	if (!args->xbuf) {
		ioq_free(ioq, ent);
		return -1;
	}
#endif

	ioq_batch_push(ioq->pending, &ioq->pending_batch, ent);
	return 0;
}

void ioq_submit(struct ioq *ioq) {
	ioq_batch_flush(ioq->pending, &ioq->pending_batch);
}

struct ioq_ent *ioq_pop(struct ioq *ioq, bool block) {
	// Don't forget to submit before popping
	bfs_assert(ioq_batch_empty(&ioq->pending_batch));

	if (ioq->size == 0) {
		return NULL;
	}

	return ioq_batch_pop(ioq->ready, &ioq->ready_batch, block);
}

void ioq_free(struct ioq *ioq, struct ioq_ent *ent) {
	bfs_assert(ioq->size > 0);
	--ioq->size;

#if BFS_WITH_LIBURING && BFS_USE_STATX
	if (ent->op == IOQ_STAT && ent->stat.xbuf) {
		arena_free(&ioq->xbufs, ent->stat.xbuf);
	}
#endif

	arena_free(&ioq->ents, ent);
}

void ioq_cancel(struct ioq *ioq) {
	if (!exchange(&ioq->cancel, true, relaxed)) {
		ioq_batch_push(ioq->pending, &ioq->pending_batch, &IOQ_STOP);
		ioq_submit(ioq);
	}
}

void ioq_destroy(struct ioq *ioq) {
	if (!ioq) {
		return;
	}

	if (ioq->nthreads > 0) {
		ioq_cancel(ioq);
	}

	for (size_t i = 0; i < ioq->nthreads; ++i) {
		ioq_thread_join(&ioq->threads[i]);
	}

	ioqq_destroy(ioq->ready);
	ioqq_destroy(ioq->pending);

#if BFS_WITH_LIBURING && BFS_USE_STATX
	arena_destroy(&ioq->xbufs);
#endif
	arena_destroy(&ioq->ents);

	free(ioq);
}
