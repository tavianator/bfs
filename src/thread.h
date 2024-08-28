// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

/**
 * Wrappers for POSIX threading APIs.
 */

#ifndef BFS_THREAD_H
#define BFS_THREAD_H

#include "prelude.h"

#include <pthread.h>

/** Thread entry point type. */
typedef void *thread_fn(void *arg);

/**
 * Wrapper for pthread_create().
 *
 * @return
 *         0 on success, -1 on error.
 */
int thread_create(pthread_t *thread, const pthread_attr_t *attr, thread_fn *fn, void *arg);

/**
 * Wrapper for pthread_join().
 */
void thread_join(pthread_t thread, void **ret);

/**
 * Wrapper for pthread_mutex_init().
 */
int mutex_init(pthread_mutex_t *mutex, pthread_mutexattr_t *attr);

/**
 * Wrapper for pthread_mutex_lock().
 */
void mutex_lock(pthread_mutex_t *mutex);

/**
 * Wrapper for pthread_mutex_trylock().
 *
 * @return
 *         Whether the mutex was locked.
 */
bool mutex_trylock(pthread_mutex_t *mutex);

/**
 * Wrapper for pthread_mutex_unlock().
 */
void mutex_unlock(pthread_mutex_t *mutex);

/**
 * Wrapper for pthread_mutex_destroy().
 */
void mutex_destroy(pthread_mutex_t *mutex);

/**
 * Wrapper for pthread_cond_init().
 */
int cond_init(pthread_cond_t *cond, pthread_condattr_t *attr);

/**
 * Wrapper for pthread_cond_wait().
 */
void cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex);

/**
 * Wrapper for pthread_cond_signal().
 */
void cond_signal(pthread_cond_t *cond);

/**
 * Wrapper for pthread_cond_broadcast().
 */
void cond_broadcast(pthread_cond_t *cond);

/**
 * Wrapper for pthread_cond_destroy().
 */
void cond_destroy(pthread_cond_t *cond);

/** pthread_once() callback type. */
typedef void once_fn(void);

/**
 * Wrapper for pthread_once().
 */
void invoke_once(pthread_once_t *once, once_fn *fn);

#endif // BFS_THREAD_H
