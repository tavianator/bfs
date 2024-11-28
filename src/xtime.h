// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

/**
 * Date/time handling.
 */

#ifndef BFS_XTIME_H
#define BFS_XTIME_H

#include <time.h>

/**
 * mktime() wrapper that reports errors more reliably.
 *
 * @tm[in,out]
 *         The struct tm to convert and normalize.
 * @timep[out]
 *         A pointer to the result.
 * @return
 *         0 on success, -1 on failure.
 */
int xmktime(struct tm *tm, time_t *timep);

/**
 * A portable timegm(), the inverse of gmtime().
 *
 * @tm[in,out]
 *         The struct tm to convert and normalize.
 * @timep[out]
 *         A pointer to the result.
 * @return
 *         0 on success, -1 on failure.
 */
int xtimegm(struct tm *tm, time_t *timep);

/**
 * Parse an ISO 8601-style timestamp.
 *
 * @str
 *         The string to parse.
 * @result[out]
 *         A pointer to the result.
 * @return
 *         0 on success, -1 on failure.
 */
int xgetdate(const char *str, struct timespec *result);

/**
 * Add to a timespec.
 */
void timespec_add(struct timespec *lhs, const struct timespec *rhs);

/**
 * Subtract from a timespec.
 */
void timespec_sub(struct timespec *lhs, const struct timespec *rhs);

/**
 * Compare two timespecs.
 *
 * @return
 *         An integer with the sign of (*lhs - *rhs).
 */
int timespec_cmp(const struct timespec *lhs, const struct timespec *rhs);

/**
 * Update a minimum timespec.
 */
void timespec_min(struct timespec *dest, const struct timespec *src);

/**
 * Update a maximum timespec.
 */
void timespec_max(struct timespec *dest, const struct timespec *src);

/**
 * Convert a timespec to floating point.
 *
 * @return
 *         The value in nanoseconds.
 */
double timespec_ns(const struct timespec *ts);

/**
 * A timer.
 */
struct timer;

/**
 * Start a timer.
 *
 * @interval
 *         The regular interval at which to send SIGALRM.
 * @return
 *         The new timer on success, otherwise NULL.
 */
struct timer *xtimer_start(const struct timespec *interval);

/**
 * Stop a timer.
 *
 * @timer
 *         The timer to stop.
 */
void xtimer_stop(struct timer *timer);

#endif // BFS_XTIME_H
