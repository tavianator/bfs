// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

/**
 * Dynamic (un)registration of signal handlers.
 *
 * Because signal handlers can interrupt any thread at an arbitrary point, they
 * must be lock-free or risk deadlock.  Therefore, we implement the global table
 * of signal "hooks" with a simple read-copy-update (RCU) scheme.  Readers get a
 * reference-counted pointer (struct arc) to the table in a lock-free way, and
 * release the reference count when finished.
 *
 * Updates are managed by struct rcu, which has two slots: one active and one
 * inactive.  Readers acquire a reference to the active slot.  A single writer
 * can safely update it by initializing the inactive slot, atomically swapping
 * the slots, and waiting for the reference count of the newly inactive slot to
 * drop to zero.  Once it does, the old pointer can be safely freed.
 */

#include "prelude.h"
#include "sighook.h"
#include "alloc.h"
#include "atomic.h"
#include "bfstd.h"
#include "diag.h"
#include "thread.h"
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

#if _POSIX_SEMAPHORES > 0
#  include <semaphore.h>
#endif

/**
 * An atomically reference-counted pointer.
 */
struct arc {
	/** The current reference count (0 means empty). */
	atomic size_t refs;
	/** The reference itself. */
	void *ptr;

#if _POSIX_SEMAPHORES > 0
	/** A semaphore for arc_wake(). */
	sem_t sem;
	/** sem_init() result. */
	int sem_status;
#endif
};

/** Initialize an arc. */
static void arc_init(struct arc *arc) {
	bfs_verify(atomic_is_lock_free(&arc->refs));

	atomic_init(&arc->refs, 0);
	arc->ptr = NULL;

#if _POSIX_SEMAPHORES > 0
	arc->sem_status = sem_init(&arc->sem, false, 0);
#endif
}

/** Get the current refcount. */
static size_t arc_refs(const struct arc *arc) {
	return load(&arc->refs, relaxed);
}

/** Set the pointer in an empty arc. */
static void arc_set(struct arc *arc, void *ptr) {
	bfs_assert(arc_refs(arc) == 0);
	bfs_assert(ptr);

	arc->ptr = ptr;
	store(&arc->refs, 1, release);
}

/** Acquire a reference. */
static void *arc_get(struct arc *arc) {
	size_t refs = arc_refs(arc);
	do {
		if (refs < 1) {
			return NULL;
		}
	} while (!compare_exchange_weak(&arc->refs, &refs, refs + 1, acquire, relaxed));

	return arc->ptr;
}

/** Release a reference. */
static void arc_put(struct arc *arc) {
	size_t refs = fetch_sub(&arc->refs, 1, release);

	if (refs == 1) {
#if _POSIX_SEMAPHORES > 0
		if (arc->sem_status == 0 && sem_post(&arc->sem) != 0) {
			abort();
		}
#endif
	}
}

/** Wait on the semaphore. */
static int arc_sem_wait(struct arc *arc) {
#if _POSIX_SEMAPHORES > 0
	if (arc->sem_status == 0) {
		while (sem_wait(&arc->sem) != 0) {
			bfs_everify(errno == EINTR, "sem_wait()");
		}
		return 0;
	}
#endif

	return -1;
}

/** Wait for all references to be released. */
static void *arc_wait(struct arc *arc) {
	size_t refs = fetch_sub(&arc->refs, 1, relaxed);
	bfs_assert(refs > 0);

	--refs;
	while (refs > 0) {
		if (arc_sem_wait(arc) == 0) {
			bfs_assert(arc_refs(arc) == 0);
			// sem_wait() provides enough ordering, so we can skip the fence
			goto done;
		}

		// Some platforms (like macOS) don't support unnamed semaphores,
		// but we can always busy-wait
		spin_loop();
		refs = arc_refs(arc);
	}

	thread_fence(&arc->refs, acquire);

done:;
	void *ptr = arc->ptr;
	arc->ptr = NULL;
	return ptr;
}

/** Destroy an arc. */
static void arc_destroy(struct arc *arc) {
	bfs_assert(arc_refs(arc) <= 1);

#if _POSIX_SEMAPHORES > 0
	if (arc->sem_status == 0) {
		bfs_everify(sem_destroy(&arc->sem) == 0, "sem_destroy()");
	}
#endif
}

/**
 * A simple read-copy-update memory reclamation scheme.
 */
struct rcu {
	/** The currently active slot. */
	atomic size_t active;
	/** The two slots. */
	struct arc slots[2];
};

/** Sentinel value for RCU, since arc uses NULL already. */
static void *RCU_NULL = &RCU_NULL;

/** Initialize an RCU block. */
static void rcu_init(struct rcu *rcu) {
	bfs_verify(atomic_is_lock_free(&rcu->active));

	atomic_init(&rcu->active, 0);
	arc_init(&rcu->slots[0]);
	arc_init(&rcu->slots[1]);
	arc_set(&rcu->slots[0], RCU_NULL);
}

/** Get the active slot. */
static struct arc *rcu_active(struct rcu *rcu) {
	size_t i = load(&rcu->active, relaxed);
	return &rcu->slots[i];
}

/** Read an RCU-protected pointer. */
static void *rcu_read(struct rcu *rcu, struct arc **slot) {
	while (true) {
		*slot = rcu_active(rcu);
		void *ptr = arc_get(*slot);
		if (ptr == RCU_NULL) {
			return NULL;
		} else if (ptr) {
			return ptr;
		}
		// Otherwise, the other slot became active; retry
	}
}

/** Get the RCU-protected pointer without acquiring a reference. */
static void *rcu_peek(struct rcu *rcu) {
	struct arc *arc = rcu_active(rcu);
	void *ptr = arc->ptr;
	if (ptr == RCU_NULL) {
		return NULL;
	} else {
		return ptr;
	}
}

/** Update an RCU-protected pointer, and return the old one. */
static void *rcu_update(struct rcu *rcu, void *ptr) {
	size_t i = load(&rcu->active, relaxed);
	struct arc *prev = &rcu->slots[i];

	size_t j = i ^ 1;
	struct arc *next = &rcu->slots[j];

	arc_set(next, ptr ? ptr : RCU_NULL);
	store(&rcu->active, j, relaxed);
	return arc_wait(prev);
}

struct sighook {
	int sig;
	sighook_fn *fn;
	void *arg;
	enum sigflags flags;
};

/**
 * A table of signal hooks.
 */
struct sigtable {
	/** The number of filled slots. */
	size_t filled;
	/** The length of the array. */
	size_t size;
	/** An array of signal hooks. */
	struct arc hooks[];
};

/** Add a hook to a table. */
static int sigtable_add(struct sigtable *table, struct sighook *hook) {
	if (!table || table->filled == table->size) {
		return -1;
	}

	for (size_t i = 0; i < table->size; ++i) {
		struct arc *arc = &table->hooks[i];
		if (arc_refs(arc) == 0) {
			arc_set(arc, hook);
			++table->filled;
			return 0;
		}
	}

	return -1;
}

/** Delete a hook from a table. */
static int sigtable_del(struct sigtable *table, struct sighook *hook) {
	for (size_t i = 0; i < table->size; ++i) {
		struct arc *arc = &table->hooks[i];
		if (arc->ptr == hook) {
			arc_wait(arc);
			--table->filled;
			return 0;
		}
	}

	return -1;
}

/** Create a bigger copy of a signal table. */
static struct sigtable *sigtable_grow(struct sigtable *prev) {
	size_t old_size = prev ? prev->size : 0;
	size_t new_size = old_size ? 2 * old_size : 1;
	struct sigtable *table = ALLOC_FLEX(struct sigtable, hooks, new_size);
	if (!table) {
		return NULL;
	}

	table->filled = 0;
	table->size = new_size;
	for (size_t i = 0; i < new_size; ++i) {
		arc_init(&table->hooks[i]);
	}

	for (size_t i = 0; i < old_size; ++i) {
		struct sighook *hook = prev->hooks[i].ptr;
		if (hook) {
			bfs_verify(sigtable_add(table, hook) == 0);
		}
	}

	return table;
}

/** Free a signal table. */
static void sigtable_free(struct sigtable *table) {
	if (!table) {
		return;
	}

	for (size_t i = 0; i < table->size; ++i) {
		struct arc *arc = &table->hooks[i];
		arc_destroy(arc);
	}
	free(table);
}

/** Add a hook to a signal table, growing it if necessary. */
static int rcu_sigtable_add(struct rcu *rcu, struct sighook *hook) {
	struct sigtable *prev = rcu_peek(rcu);
	if (sigtable_add(prev, hook) == 0) {
		return 0;
	}

	struct sigtable *next = sigtable_grow(prev);
	if (!next) {
		return -1;
	}

	bfs_verify(sigtable_add(next, hook) == 0);
	rcu_update(rcu, next);
	sigtable_free(prev);
	return 0;
}

/** The global table of signal hooks. */
static struct rcu rcu_sighooks;
/** The global table of atsigexit() hooks. */
static struct rcu rcu_exithooks;

/** Mutex for initialization and RCU writer exclusion. */
static pthread_mutex_t sigmutex = PTHREAD_MUTEX_INITIALIZER;

/** Check if a signal was generated by userspace. */
static bool is_user_generated(const siginfo_t *info) {
	// https://pubs.opengroup.org/onlinepubs/9699919799/functions/V2_chap02.html#tag_15_04_03
	//
	//     If si_code is SI_USER or SI_QUEUE, or any value less than or
	//     equal to 0, then the signal was generated by a process ...
	int code = info->si_code;
	return code == SI_USER || code == SI_QUEUE || code <= 0;
}

/** Check if a signal is caused by a fault. */
static bool is_fault(const siginfo_t *info) {
	int sig = info->si_signo;
	if (sig == SIGBUS || sig == SIGFPE || sig == SIGILL || sig == SIGSEGV) {
		return !is_user_generated(info);
	} else {
		return false;
	}
}

// https://pubs.opengroup.org/onlinepubs/9699919799/basedefs/signal.h.html
static const int FATAL_SIGNALS[] = {
	SIGABRT,
	SIGALRM,
	SIGBUS,
	SIGFPE,
	SIGHUP,
	SIGILL,
	SIGINT,
	SIGPIPE,
	SIGQUIT,
	SIGSEGV,
	SIGTERM,
	SIGUSR1,
	SIGUSR2,
#ifdef SIGPOLL
	SIGPOLL,
#endif
#ifdef SIGPROF
	SIGPROF,
#endif
#ifdef SIGSYS
	SIGSYS,
#endif
	SIGTRAP,
#ifdef SIGVTALRM
	SIGVTALRM,
#endif
	SIGXCPU,
	SIGXFSZ,
};

/** Check if a signal's default action is to terminate the process. */
static bool is_fatal(int sig) {
	for (size_t i = 0; i < countof(FATAL_SIGNALS); ++i) {
		if (sig == FATAL_SIGNALS[i]) {
			return true;
		}
	}

#ifdef SIGRTMIN
	// https://pubs.opengroup.org/onlinepubs/9699919799/functions/V2_chap02.html#tag_15_04_03
	//
	//     The default actions for the realtime signals in the range
	//     SIGRTMIN to SIGRTMAX shall be to terminate the process
	//     abnormally.
	if (sig >= SIGRTMIN && sig <= SIGRTMAX) {
		return true;
	}
#endif

	return false;
}

/** Reraise a fatal signal. */
static noreturn void reraise(int sig) {
	// Restore the default signal action
	if (signal(sig, SIG_DFL) == SIG_ERR) {
		goto fail;
	}

	// Unblock the signal, since we didn't set SA_NODEFER
	sigset_t mask;
	if (sigemptyset(&mask) != 0
	    || sigaddset(&mask, sig) != 0
	    || pthread_sigmask(SIG_UNBLOCK, &mask, NULL) != 0) {
		goto fail;
	}

	raise(sig);
fail:
	abort();
}

/** Find any matching hooks and run them. */
static enum sigflags run_hooks(struct rcu *rcu, int sig, siginfo_t *info) {
	enum sigflags ret = 0;
	struct arc *slot;
	struct sigtable *table = rcu_read(rcu, &slot);
	if (!table) {
		goto done;
	}

	for (size_t i = 0; i < table->size; ++i) {
		struct arc *arc = &table->hooks[i];
		struct sighook *hook = arc_get(arc);
		if (!hook) {
			continue;
		}

		if (hook->sig == sig || hook->sig == 0) {
			hook->fn(sig, info, hook->arg);
			ret |= hook->flags;
		}
		arc_put(arc);
	}

done:
	arc_put(slot);
	return ret;
}

/** Dispatches a signal to the registered handlers. */
static void sigdispatch(int sig, siginfo_t *info, void *context) {
	// https://pubs.opengroup.org/onlinepubs/9699919799/functions/V2_chap02.html#tag_15_04_03
	//
	//     The behavior of a process is undefined after it returns normally
	//     from a signal-catching function for a SIGBUS, SIGFPE, SIGILL, or
	//     SIGSEGV signal that was not generated by kill(), sigqueue(), or
	//     raise().
	if (is_fault(info)) {
		reraise(sig);
	}

	// https://pubs.opengroup.org/onlinepubs/9699919799/functions/V2_chap02.html#tag_15_04_03
	//
	//     After returning from a signal-catching function, the value of
	//     errno is unspecified if the signal-catching function or any
	//     function it called assigned a value to errno and the signal-
	//     catching function did not save and restore the original value of
	//     errno.
	int error = errno;

	// Run the normal hooks
	enum sigflags flags = run_hooks(&rcu_sighooks, sig, info);

	// Run the atsigexit() hooks, if we're exiting
	if (!(flags & SH_CONTINUE) && is_fatal(sig)) {
		run_hooks(&rcu_exithooks, sig, info);
		reraise(sig);
	}

	errno = error;
}

/** Make sure our signal handler is installed for a given signal. */
static int siginit(int sig) {
	static struct sigaction action = {
		.sa_sigaction = sigdispatch,
		.sa_flags = SA_RESTART | SA_SIGINFO,
	};

	static sigset_t signals;
	static bool initialized = false;

	if (!initialized) {
		if (sigemptyset(&signals) != 0
		    || sigemptyset(&action.sa_mask) != 0) {
			return -1;
		}
		rcu_init(&rcu_sighooks);
		rcu_init(&rcu_exithooks);
		initialized = true;
	}

	int installed = sigismember(&signals, sig);
	if (installed < 0) {
		return -1;
	} else if (installed) {
		return 0;
	}

	if (sigaction(sig, &action, NULL) != 0) {
		return -1;
	}

	if (sigaddset(&signals, sig) != 0) {
		return -1;
	}

	return 0;
}

/** Shared sighook()/atsigexit() implementation. */
static struct sighook *sighook_impl(struct rcu *rcu, int sig, sighook_fn *fn, void *arg, enum sigflags flags) {
	struct sighook *hook = ALLOC(struct sighook);
	if (!hook) {
		return NULL;
	}

	hook->sig = sig;
	hook->fn = fn;
	hook->arg = arg;
	hook->flags = flags;

	if (rcu_sigtable_add(rcu, hook) != 0) {
		free(hook);
		return NULL;
	}

	return hook;
}

struct sighook *sighook(int sig, sighook_fn *fn, void *arg, enum sigflags flags) {
	mutex_lock(&sigmutex);

	struct sighook *ret = NULL;
	if (siginit(sig) != 0) {
		goto done;
	}

	ret = sighook_impl(&rcu_sighooks, sig, fn, arg, flags);
done:
	mutex_unlock(&sigmutex);
	return ret;
}

struct sighook *atsigexit(sighook_fn *fn, void *arg) {
	mutex_lock(&sigmutex);

	for (size_t i = 0; i < countof(FATAL_SIGNALS); ++i) {
		// Ignore errors; atsigexit() is best-effort anyway and things
		// like sanitizer runtimes or valgrind may reserve signals for
		// their own use
		siginit(FATAL_SIGNALS[i]);
	}

#ifdef SIGRTMIN
	for (int i = SIGRTMIN; i <= SIGRTMAX; ++i) {
		siginit(i);
	}
#endif

	struct sighook *ret = sighook_impl(&rcu_exithooks, 0, fn, arg, 0);
	mutex_unlock(&sigmutex);
	return ret;
}

void sigunhook(struct sighook *hook) {
	if (!hook) {
		return;
	}

	mutex_lock(&sigmutex);

	struct rcu *rcu = hook->sig ? &rcu_sighooks : &rcu_exithooks;
	struct sigtable *table = rcu_peek(rcu);
	bfs_verify(sigtable_del(table, hook) == 0);

	if (table->filled == 0) {
		rcu_update(rcu, NULL);
		sigtable_free(table);
	}

	mutex_unlock(&sigmutex);
	free(hook);
}
