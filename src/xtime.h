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
 * @tm (in+out)
 *         The struct tm to convert.
 * @timep (out)
 *         A pointer to the result.
 * @return
 *         0 on success, -1 on failure.
 */
int xmktime(struct tm *tm, time_t *timep);

/**
 * A portable timegm(), the inverse of gmtime().
 *
 * @tm (in+out)
 *         The struct tm to convert.
 * @timep (out)
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
 * @result (out)
 *         A pointer to the result.
 * @return
 *         0 on success, -1 on failure.
 */
int xgetdate(const char *str, struct timespec *result);

#endif // BFS_XTIME_H
