// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

/**
 * Wrappers for POSIX threading APIs.
 */

#ifndef BFS_THREAD_H
#define BFS_THREAD_H

#include "diag.h"
#include <errno.h>
#include <pthread.h>
#include <string.h>

#define thread_verify(expr, cond) \
	bfs_verify((errno = (expr), (cond)), "%s: %s", #expr, strerror(errno))

/**
 * Wrapper for pthread_create().
 *
 * @return
 *         0 on success, -1 on error.
 */
#define thread_create(thread, attr, fn, arg) \
	((errno = pthread_create(thread, attr, fn, arg)) ? -1 : 0)

/**
 * Wrapper for pthread_join().
 */
#define thread_join(thread, ret) \
	thread_verify(pthread_join(thread, ret), errno == 0)

/**
 * Wrapper for pthread_mutex_init().
 */
#define mutex_init(mutex, attr) \
	((errno = pthread_mutex_init(mutex, attr)) ? -1 : 0)

/**
 * Wrapper for pthread_mutex_lock().
 */
#define mutex_lock(mutex) \
	thread_verify(pthread_mutex_lock(mutex), errno == 0)

/**
 * Wrapper for pthread_mutex_trylock().
 *
 * @return
 *         Whether the mutex was locked.
 */
#define mutex_trylock(mutex) \
	(thread_verify(pthread_mutex_trylock(mutex), errno == 0 || errno == EBUSY), errno == 0)

/**
 * Wrapper for pthread_mutex_unlock().
 */
#define mutex_unlock(mutex) \
	thread_verify(pthread_mutex_unlock(mutex), errno == 0)

/**
 * Wrapper for pthread_mutex_destroy().
 */
#define mutex_destroy(mutex) \
	thread_verify(pthread_mutex_destroy(mutex), errno == 0)

/**
 * Wrapper for pthread_cond_init().
 */
#define cond_init(cond, attr) \
	((errno = pthread_cond_init(cond, attr)) ? -1 : 0)

/**
 * Wrapper for pthread_cond_wait().
 */
#define cond_wait(cond, mutex) \
	thread_verify(pthread_cond_wait(cond, mutex), errno == 0)

/**
 * Wrapper for pthread_cond_signal().
 */
#define cond_signal(cond) \
	thread_verify(pthread_cond_signal(cond), errno == 0)

/**
 * Wrapper for pthread_cond_broadcast().
 */
#define cond_broadcast(cond) \
	thread_verify(pthread_cond_broadcast(cond), errno == 0)

/**
 * Wrapper for pthread_cond_destroy().
 */
#define cond_destroy(cond) \
	thread_verify(pthread_cond_destroy(cond), errno == 0)

/**
 * Wrapper for pthread_once().
 */
#define call_once(once, fn) \
	thread_verify(pthread_once(once, fn), errno == 0)

#endif // BFS_THREAD_H
