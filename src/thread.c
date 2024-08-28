// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include "thread.h"

#include "bfstd.h"
#include "diag.h"

#include <errno.h>
#include <pthread.h>

#define THREAD_FALLIBLE(expr) \
	do { \
		int err = expr; \
		if (err == 0) { \
			return 0; \
		} else { \
			errno = err; \
			return -1; \
		} \
	} while (0)

#define THREAD_INFALLIBLE(...) \
	THREAD_INFALLIBLE_(__VA_ARGS__, 0, )

#define THREAD_INFALLIBLE_(expr, allowed, ...) \
	int err = expr; \
	bfs_verify(err == 0 || err == allowed, "%s: %s", #expr, xstrerror(err)); \
	(void)0

int thread_create(pthread_t *thread, const pthread_attr_t *attr, thread_fn *fn, void *arg) {
	THREAD_FALLIBLE(pthread_create(thread, attr, fn, arg));
}

void thread_join(pthread_t thread, void **ret) {
	THREAD_INFALLIBLE(pthread_join(thread, ret));
}

int mutex_init(pthread_mutex_t *mutex, pthread_mutexattr_t *attr) {
	THREAD_FALLIBLE(pthread_mutex_init(mutex, attr));
}

void mutex_lock(pthread_mutex_t *mutex) {
	THREAD_INFALLIBLE(pthread_mutex_lock(mutex));
}

bool mutex_trylock(pthread_mutex_t *mutex) {
	THREAD_INFALLIBLE(pthread_mutex_trylock(mutex), EBUSY);
	return err == 0;
}

void mutex_unlock(pthread_mutex_t *mutex) {
	THREAD_INFALLIBLE(pthread_mutex_unlock(mutex));
}

void mutex_destroy(pthread_mutex_t *mutex) {
	THREAD_INFALLIBLE(pthread_mutex_destroy(mutex));
}

int cond_init(pthread_cond_t *cond, pthread_condattr_t *attr) {
	THREAD_FALLIBLE(pthread_cond_init(cond, attr));
}

void cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex) {
	THREAD_INFALLIBLE(pthread_cond_wait(cond, mutex));
}

void cond_signal(pthread_cond_t *cond) {
	THREAD_INFALLIBLE(pthread_cond_signal(cond));
}

void cond_broadcast(pthread_cond_t *cond) {
	THREAD_INFALLIBLE(pthread_cond_broadcast(cond));
}

void cond_destroy(pthread_cond_t *cond) {
	THREAD_INFALLIBLE(pthread_cond_destroy(cond));
}

void invoke_once(pthread_once_t *once, once_fn *fn) {
	THREAD_INFALLIBLE(pthread_once(once, fn));
}
