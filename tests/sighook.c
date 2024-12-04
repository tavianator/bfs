// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include "tests.h"

#include "atomic.h"
#include "sighook.h"
#include "thread.h"
#include "xtime.h"

#include <stddef.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>

/** Counts SIGALRM deliveries. */
static atomic size_t count = 0;

/** Keeps the background thread alive. */
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
static bool done = false;

/** SIGALRM handler. */
static void alrm_hook(int sig, siginfo_t *info, void *arg) {
	fetch_add(&count, 1, relaxed);
}

/** SH_ONESHOT counter. */
static atomic size_t shots = 0;

/** SH_ONESHOT hook. */
static void alrm_oneshot(int sig, siginfo_t *info, void *arg) {
	fetch_add(&shots, 1, relaxed);
}

/** Background thread that receives signals. */
static void *hook_thread(void *ptr) {
	mutex_lock(&mutex);
	while (!done) {
		cond_wait(&cond, &mutex);
	}
	mutex_unlock(&mutex);
	return NULL;
}

/** Block a signal in this thread. */
static int block_signal(int sig, sigset_t *old) {
	sigset_t set;
	if (sigemptyset(&set) != 0) {
		return -1;
	}
	if (sigaddset(&set, sig) != 0) {
		return -1;
	}

	errno = pthread_sigmask(SIG_BLOCK, &set, old);
	if (errno != 0) {
		return -1;
	}

	return 0;
}

void check_sighook(void) {
	struct sighook *hook = sighook(SIGALRM, alrm_hook, NULL, SH_CONTINUE);
	if (!bfs_echeck(hook, "sighook(SIGALRM)")) {
		return;
	}

	// Check that we can unregister and re-register a hook
	sigunhook(hook);
	hook = sighook(SIGALRM, alrm_hook, NULL, SH_CONTINUE);
	if (!bfs_echeck(hook, "sighook(SIGALRM)")) {
		return;
	}

	// Test SH_ONESHOT
	struct sighook *oneshot_hook = sighook(SIGALRM, alrm_oneshot, NULL, SH_ONESHOT);
	if (!bfs_echeck(oneshot_hook, "sighook(SH_ONESHOT)")) {
		goto unhook;
	}

	// Create a timer that sends SIGALRM every 100 microseconds
	struct timespec ival = { .tv_nsec = 100 * 1000 };
	struct timer *timer = xtimer_start(&ival);
	if (!bfs_echeck(timer)) {
		goto unhook;
	}

	// Create a background thread to receive signals
	pthread_t thread;
	if (!bfs_echeck(thread_create(&thread, NULL, hook_thread, NULL) == 0)) {
		goto untime;
	}

	// Block SIGALRM in this thread so the handler runs concurrently with
	// sighook()/sigunhook()
	sigset_t mask;
	if (!bfs_echeck(block_signal(SIGALRM, &mask) == 0)) {
		goto untime;
	}

	// Rapidly register/unregister SIGALRM hooks
	size_t alarms;
	while (alarms = load(&count, relaxed), alarms < 1000) {
		size_t nshots = load(&shots, relaxed);
		bfs_echeck(nshots <= 1);
		if (alarms > 1) {
			bfs_echeck(nshots == 1);
		}
		if (alarms >= 500) {
			sigunhook(oneshot_hook);
			oneshot_hook = NULL;
		}

		struct sighook *next = sighook(SIGALRM, alrm_hook, NULL, SH_CONTINUE);
		if (!bfs_echeck(next, "sighook(SIGALRM)")) {
			break;
		}

		sigunhook(hook);
		hook = next;
	}

	// Quit the background thread
	mutex_lock(&mutex);
	done = true;
	mutex_unlock(&mutex);
	cond_signal(&cond);
	thread_join(thread, NULL);

	// Restore the old signal mask
	errno = pthread_sigmask(SIG_SETMASK, &mask, NULL);
	bfs_echeck(errno == 0, "pthread_sigmask()");
untime:
	// Stop the timer
	xtimer_stop(timer);
unhook:
	// Unregister the SIGALRM hooks
	sigunhook(oneshot_hook);
	sigunhook(hook);
}
