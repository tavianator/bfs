/****************************************************************************
 * bfs                                                                      *
 * Copyright (C) 2020 Tavian Barnes <tavianator@tavianator.com>             *
 *                                                                          *
 * Permission to use, copy, modify, and/or distribute this software for any *
 * purpose with or without fee is hereby granted.                           *
 *                                                                          *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES *
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF         *
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR  *
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES   *
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN    *
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF  *
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.           *
 ****************************************************************************/

/**
 * Date/time handling.
 */

#ifndef BFS_TIME_H
#define BFS_TIME_H

#include <time.h>

/**
 * localtime_r() wrapper that calls tzset() first.
 *
 * @param[in] timep
 *         The time_t to convert.
 * @param[out] result
 *         Buffer to hold the result.
 * @return
 *         0 on success, -1 on failure.
 */
int xlocaltime(const time_t *timep, struct tm *result);

/**
 * gmtime_r() wrapper that calls tzset() first.
 *
 * @param[in] timep
 *         The time_t to convert.
 * @param[out] result
 *         Buffer to hold the result.
 * @return
 *         0 on success, -1 on failure.
 */
int xgmtime(const time_t *timep, struct tm *result);

/**
 * mktime() wrapper that reports errors more reliably.
 *
 * @param[in,out] tm
 *         The struct tm to convert.
 * @param[out] timep
 *         A pointer to the result.
 * @return
 *         0 on success, -1 on failure.
 */
int xmktime(struct tm *tm, time_t *timep);

/**
 * A portable timegm(), the inverse of gmtime().
 *
 * @param[in,out] tm
 *         The struct tm to convert.
 * @param[out] timep
 *         A pointer to the result.
 * @return
 *         0 on success, -1 on failure.
 */
int xtimegm(struct tm *tm, time_t *timep);

/**
 * Parse an ISO 8601-style timestamp.
 *
 * @param[in] str
 *         The string to parse.
 * @param[out] result
 *         A pointer to the result.
 * @return
 *         0 on success, -1 on failure.
 */
int parse_timestamp(const char *str, struct timespec *result);

#endif // BFS_TIME_H
