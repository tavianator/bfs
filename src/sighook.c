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

#include "sighook.h"

#include "alloc.h"
#include "atomic.h"
#include "bfs.h"
#include "bfstd.h"
#include "diag.h"
#include "thread.h"

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

#if __linux__
#  include <sys/syscall.h>
#endif

// NetBSD opens a file descriptor for each sem_init()
#if defined(_POSIX_SEMAPHORES) && !__NetBSD__
#  define BFS_POSIX_SEMAPHORES _POSIX_SEMAPHORES
#else
#  define BFS_POSIX_SEMAPHORES (-1)
#endif

#if BFS_POSIX_SEMAPHORES >= 0
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

#if BFS_POSIX_SEMAPHORES >= 0
	/** A semaphore for arc_wait(). */
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

#if BFS_POSIX_SEMAPHORES >= 0
	if (sysoption(SEMAPHORES) > 0) {
		arc->sem_status = sem_init(&arc->sem, false, 0);
	} else {
		arc->sem_status = -1;
	}
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
#if BFS_POSIX_SEMAPHORES >= 0
		if (arc->sem_status == 0 && sem_post(&arc->sem) != 0) {
			abort();
		}
#endif
	}
}

/** Wait on the semaphore. */
static int arc_sem_wait(struct arc *arc) {
#if BFS_POSIX_SEMAPHORES >= 0
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
	bfs_assert(arc_refs(arc) == 0);

#if BFS_POSIX_SEMAPHORES >= 0
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

/** Map NULL -> RCU_NULL. */
static void *rcu_encode(void *ptr) {
	return ptr ? ptr : RCU_NULL;
}

/** Map RCU_NULL -> NULL. */
static void *rcu_decode(void *ptr) {
	bfs_assert(ptr != NULL);
	return ptr == RCU_NULL ? NULL : ptr;
}

/** Initialize an RCU block. */
static void rcu_init(struct rcu *rcu, void *ptr) {
	bfs_verify(atomic_is_lock_free(&rcu->active));

	atomic_init(&rcu->active, 0);
	arc_init(&rcu->slots[0]);
	arc_init(&rcu->slots[1]);
	arc_set(&rcu->slots[0], rcu_encode(ptr));
}

/** Get the active slot. */
static struct arc *rcu_active(struct rcu *rcu) {
	size_t i = load(&rcu->active, relaxed);
	return &rcu->slots[i];
}

/** Destroy an RCU block. */
static void rcu_destroy(struct rcu *rcu) {
	arc_wait(rcu_active(rcu));
	arc_destroy(&rcu->slots[1]);
	arc_destroy(&rcu->slots[0]);
}

/** Read an RCU-protected pointer. */
static void *rcu_read(struct rcu *rcu, struct arc **slot) {
	while (true) {
		*slot = rcu_active(rcu);
		void *ptr = arc_get(*slot);
		if (ptr) {
			return rcu_decode(ptr);
		}
		// Otherwise, the other slot became active; retry
	}
}

/** Get the RCU-protected pointer without acquiring a reference. */
static void *rcu_peek(struct rcu *rcu) {
	struct arc *arc = rcu_active(rcu);
	return rcu_decode(arc->ptr);
}

/** Update an RCU-protected pointer, and return the old one. */
static void *rcu_update(struct rcu *rcu, void *ptr) {
	size_t i = load(&rcu->active, relaxed);
	struct arc *prev = &rcu->slots[i];

	size_t j = i ^ 1;
	struct arc *next = &rcu->slots[j];

	arc_set(next, rcu_encode(ptr));
	store(&rcu->active, j, relaxed);
	return rcu_decode(arc_wait(prev));
}

/**
 * An RCU-protected linked list.
 */
struct rcu_list {
	/** The first node in the list. */
	struct rcu head;
	/** &last->next */
	struct rcu *tail;
};

/**
 * An rcu_list node.
 */
struct rcu_node {
	/** The RCU pointer to this node. */
	struct rcu *self;
	/** The next node in the list. */
	struct rcu next;
};

/** Initialize an rcu_list. */
static void rcu_list_init(struct rcu_list *list) {
	rcu_init(&list->head, NULL);
	list->tail = &list->head;
}

/** Append to an rcu_list. */
static void rcu_list_append(struct rcu_list *list, struct rcu_node *node) {
	node->self = list->tail;
	list->tail = &node->next;
	rcu_init(&node->next, NULL);
	rcu_update(node->self, node);
}

/** Remove from an rcu_list. */
static void rcu_list_remove(struct rcu_list *list, struct rcu_node *node) {
	struct rcu_node *next = rcu_peek(&node->next);
	rcu_update(node->self, next);
	if (next) {
		next->self = node->self;
	} else {
		list->tail = &list->head;
	}
	rcu_destroy(&node->next);
}

/** Iterate over an rcu_list. */
#define for_rcu(type, node, list) \
	for_rcu_(type, node, (list), node##_slot_, node##_prev_, node##_done_)

#define for_rcu_(type, node, list, slot, prev, done) \
	/* This outer loop is just for declaring variables; it iterates once. */ \
	for (struct arc *slot, *prev, **done = NULL; !done && (done = &slot); ) \
		for (type *node = rcu_read(&list->head, &slot); \
		     node || (arc_put(slot), false); \
		     (prev = slot, \
		      node = rcu_read(&((struct rcu_node *)node)->next, &slot), \
		      arc_put(prev)))

struct sighook {
	/** The RCU list node (must be the first field). */
	struct rcu_node node;

	/** The signal being hooked, or 0 for atsigexit(). */
	int sig;
	/** Signal hook flags. */
	enum sigflags flags;
	/** The function to call. */
	sighook_fn *fn;
	/** An argument to pass to the function. */
	void *arg;
	/** Flag for SH_ONESHOT. */
	atomic bool armed;
};

/** The lists of signal hooks. */
static struct rcu_list sighooks[64];

/** Get the hook list for a particular signal. */
static struct rcu_list *siglist(int sig) {
	return &sighooks[sig % countof(sighooks)];
}

/** Mutex for initialization and RCU writer exclusion. */
static pthread_mutex_t sigmutex = PTHREAD_MUTEX_INITIALIZER;

/** Check if a signal was generated by userspace. */
static bool is_user_generated(const siginfo_t *info) {
	// https://pubs.opengroup.org/onlinepubs/9799919799/functions/V2_chap02.html#tag_16_04_03_03
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

// https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/signal.h.html
static const int FATAL_SIGNALS[] = {
	SIGABRT,
	SIGALRM,
	SIGBUS,
	SIGFPE,
	SIGHUP,
	SIGILL,
	SIGINT,
#ifdef SIGIO
	SIGIO,
#endif
	SIGPIPE,
#ifdef SIGPOLL
	SIGPOLL,
#endif
#ifdef SIGPROF
	SIGPROF,
#endif
#ifdef SIGPWR
	SIGPWR,
#endif
	SIGQUIT,
	SIGSEGV,
#ifdef SIGSTKFLT
	SIGSTKFLT,
#endif
#ifdef SIGSYS
	SIGSYS,
#endif
	SIGTERM,
	SIGTRAP,
	SIGUSR1,
	SIGUSR2,
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
	// https://pubs.opengroup.org/onlinepubs/9799919799/functions/V2_chap02.html#tag_16_04_03_01
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
_noreturn
static void reraise(siginfo_t *info) {
	int sig = info->si_signo;

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

#if __linux__
	// On Linux, try to re-raise the exact siginfo_t (since 3.9, a process can
	// signal itself with any siginfo_t)
	pid_t tid = syscall(SYS_gettid);
	syscall(SYS_rt_tgsigqueueinfo, getpid(), tid, sig, info);
#endif

	raise(sig);
fail:
	abort();
}

/** Check whether we should run a hook. */
static bool should_run(int sig, struct sighook *hook) {
	if (hook->sig != sig && hook->sig != 0) {
		return false;
	}

	if (hook->flags & SH_ONESHOT) {
		if (!exchange(&hook->armed, false, relaxed)) {
			return false;
		}
	}

	return true;
}

/** Find any matching hooks and run them. */
static enum sigflags run_hooks(struct rcu_list *list, int sig, siginfo_t *info) {
	enum sigflags ret = 0;

	for_rcu (struct sighook, hook, list) {
		if (should_run(sig, hook)) {
			hook->fn(sig, info, hook->arg);
			ret |= hook->flags;
		}
	}

	return ret;
}

/** Dispatches a signal to the registered handlers. */
static void sigdispatch(int sig, siginfo_t *info, void *context) {
	// If we get a fault (e.g. a "real" SIGSEGV, not something like
	// kill(..., SIGSEGV)), don't try to run signal hooks, since we could be
	// in an arbitrarily corrupted state.
	//
	// POSIX says that returning normally from a signal handler for a fault
	// is undefined.  But in practice, it's better to uninstall the handler
	// and return, which will re-run the faulting instruction and cause us
	// to die "correctly" (e.g. with a core dump pointing at the faulting
	// instruction, not reraise()).
	if (is_fault(info)) {
		// On macOS, we cannot reliably distinguish between faults and
		// asynchronous signals.  For example, pkill -SEGV bfs will
		// result in si_code == SEGV_ACCERR.  So we always re-raise the
		// signal, because just returning would cause us to ignore
		// asynchronous SIG{BUS,ILL,SEGV}.
#if !__APPLE__
		if (signal(sig, SIG_DFL) != SIG_ERR) {
			return;
		}
#endif
		reraise(info);
	}

	// https://pubs.opengroup.org/onlinepubs/9799919799/functions/V2_chap02.html#tag_16_04_04
	//
	//     After returning from a signal-catching function, the value of
	//     errno is unspecified if the signal-catching function or any
	//     function it called assigned a value to errno and the signal-
	//     catching function did not save and restore the original value of
	//     errno.
	int error = errno;

	// Run the normal hooks
	struct rcu_list *list = siglist(sig);
	enum sigflags flags = run_hooks(list, sig, info);

	// Run the atsigexit() hooks, if we're exiting
	if (!(flags & SH_CONTINUE) && is_fatal(sig)) {
		list = siglist(0);
		run_hooks(list, sig, info);
		reraise(info);
	}

	errno = error;
}

/** A saved signal handler, for sigreset() to restore. */
struct sigsave {
	struct rcu_node node;
	int sig;
	struct sigaction action;
};

/** The list of saved signal handlers. */
static struct rcu_list saved;
/** `saved` initialization status (since rcu_list_init() isn't atomic). */
static atomic bool initialized = false;

/** Make sure our signal handler is installed for a given signal. */
static int siginit(int sig) {
#ifdef SA_RESTART
#  define BFS_SA_RESTART SA_RESTART
#else
#  define BFS_SA_RESTART 0
#endif

	static struct sigaction action = {
		.sa_sigaction = sigdispatch,
		.sa_flags = BFS_SA_RESTART | SA_SIGINFO,
	};

	static sigset_t signals;

	if (!load(&initialized, relaxed)) {
		if (sigemptyset(&signals) != 0
		    || sigemptyset(&action.sa_mask) != 0) {
			return -1;
		}

		for (size_t i = 0; i < countof(sighooks); ++i) {
			rcu_list_init(&sighooks[i]);
		}

		rcu_list_init(&saved);
		store(&initialized, true, release);
	}

	int installed = sigismember(&signals, sig);
	if (installed < 0) {
		return -1;
	} else if (installed) {
		return 0;
	}

	struct sigsave *save = ALLOC(struct sigsave);
	if (!save) {
		return -1;
	}

	save->sig = sig;
	if (sigaction(sig, NULL, &save->action) != 0) {
		free(save);
		return -1;
	}
	rcu_list_append(&saved, &save->node);

	if (sigaction(sig, &action, NULL) != 0) {
		return -1;
	}

	if (sigaddset(&signals, sig) != 0) {
		return -1;
	}

	return 0;
}

/** Shared sighook()/atsigexit() implementation. */
static struct sighook *sighook_impl(int sig, sighook_fn *fn, void *arg, enum sigflags flags) {
	struct sighook *hook = ALLOC(struct sighook);
	if (!hook) {
		return NULL;
	}

	hook->sig = sig;
	hook->flags = flags;
	hook->fn = fn;
	hook->arg = arg;
	atomic_init(&hook->armed, true);

	struct rcu_list *list = siglist(sig);
	rcu_list_append(list, &hook->node);
	return hook;
}

struct sighook *sighook(int sig, sighook_fn *fn, void *arg, enum sigflags flags) {
	bfs_assert(sig > 0);

	mutex_lock(&sigmutex);

	struct sighook *ret = NULL;
	if (siginit(sig) == 0) {
		ret = sighook_impl(sig, fn, arg, flags);
	}

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

	struct sighook *ret = sighook_impl(0, fn, arg, 0);
	mutex_unlock(&sigmutex);
	return ret;
}

void sigunhook(struct sighook *hook) {
	if (!hook) {
		return;
	}

	mutex_lock(&sigmutex);

	struct rcu_list *list = siglist(hook->sig);
	rcu_list_remove(list, &hook->node);

	mutex_unlock(&sigmutex);

	free(hook);
}

int sigreset(void) {
	for_rcu (struct sigsave, save, &saved) {
		if (sigaction(save->sig, &save->action, NULL) != 0) {
			return -1;
		}
	}

	return 0;
}
