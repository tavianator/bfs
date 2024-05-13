// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include "prelude.h"
#include "tests.h"
#include "sighook.h"
#include "atomic.h"
#include "thread.h"
#include <pthread.h>
#include <signal.h>
#include <sys/time.h>

static atomic size_t count = 0;

/** SIGALRM handler. */
static void alrm_hook(int sig, siginfo_t *info, void *arg) {
	fetch_add(&count, 1, relaxed);
}

/** Swap out an old hook for a new hook. */
static int swap_hooks(struct sighook **hook) {
	struct sighook *next = sighook(SIGALRM, alrm_hook, NULL, SH_CONTINUE);
	if (!bfs_pcheck(next, "sighook(SIGALRM)")) {
		return -1;
	}

	sigunhook(*hook);
	*hook = next;
	return 0;
}

/** Background thread that rapidly (un)registers signal hooks. */
static void *hook_thread(void *ptr) {
	struct sighook *hook = sighook(SIGALRM, alrm_hook, NULL, SH_CONTINUE);
	if (!bfs_pcheck(hook, "sighook(SIGALRM)")) {
		return NULL;
	}

	while (load(&count, relaxed) < 1000) {
		if (swap_hooks(&hook) != 0) {
			sigunhook(hook);
			return NULL;
		}
	}

	sigunhook(hook);
	return &count;
}

bool check_sighook(void) {
	bool ret = true;

	struct sighook *hook = sighook(SIGALRM, alrm_hook, NULL, SH_CONTINUE);
	ret &= bfs_pcheck(hook, "sighook(SIGALRM)");
	if (!ret) {
		goto done;
	}

	struct itimerval ival = {
		.it_value = {
			.tv_usec = 100,
		},
		.it_interval = {
			.tv_usec = 100,
		},
	};
	ret &= bfs_pcheck(setitimer(ITIMER_REAL, &ival, NULL) == 0);
	if (!ret) {
		goto unhook;
	}

	pthread_t thread;
	ret &= bfs_pcheck(thread_create(&thread, NULL, hook_thread, NULL) == 0);
	if (!ret) {
		goto untime;
	}

	while (ret && load(&count, relaxed) < 1000) {
		ret &= swap_hooks(&hook) == 0;
	}

 	void *ptr;
	thread_join(thread, &ptr);
	ret &= bfs_check(ptr);

untime:
	ival.it_value.tv_usec = 0;
	ret &= bfs_pcheck(setitimer(ITIMER_REAL, &ival, NULL) == 0);
	if (!ret) {
		goto unhook;
	}

unhook:
	sigunhook(hook);
done:
	return ret;
}
