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

#include "time.h"
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <time.h>

int xlocaltime(const time_t *timep, struct tm *result) {
	// Should be called before localtime_r() according to POSIX.1-2004
	tzset();

	if (localtime_r(timep, result)) {
		return 0;
	} else {
		return -1;
	}
}

int xgmtime(const time_t *timep, struct tm *result) {
	// Should be called before gmtime_r() according to POSIX.1-2004
	tzset();

	if (gmtime_r(timep, result)) {
		return 0;
	} else {
		return -1;
	}
}

int xmktime(struct tm *tm, time_t *timep) {
	*timep = mktime(tm);

	if (*timep == -1) {
		int error = errno;

		struct tm tmp;
		if (xlocaltime(timep, &tmp) != 0) {
			return -1;
		}

		if (tm->tm_year != tmp.tm_year || tm->tm_yday != tmp.tm_yday
		    || tm->tm_hour != tmp.tm_hour || tm->tm_min != tmp.tm_min || tm->tm_sec != tmp.tm_sec) {
			errno = error;
			return -1;
		}
	}

	return 0;
}

static int safe_add(int *value, int delta) {
	if (*value >= 0) {
		if (delta > INT_MAX - *value) {
			return -1;
		}
	} else {
		if (delta < INT_MIN - *value) {
			return -1;
		}
	}

	*value += delta;
	return 0;
}

static int floor_div(int n, int d) {
	int a = n < 0;
	return (n + a)/d - a;
}

static int wrap(int *value, int max, int *next) {
	int carry = floor_div(*value, max);
	*value -= carry * max;
	return safe_add(next, carry);
}

static int month_length(int year, int month) {
	static const int month_lengths[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	int ret = month_lengths[month];
	if (month == 1 && year%4 == 0 && (year%100 != 0 || (year + 300)%400 == 0)) {
		++ret;
	}
	return ret;
}

int xtimegm(struct tm *tm, time_t *timep) {
	tm->tm_isdst = 0;

	if (wrap(&tm->tm_sec, 60, &tm->tm_min) != 0) {
		goto overflow;
	}
	if (wrap(&tm->tm_min, 60, &tm->tm_hour) != 0) {
		goto overflow;
	}
	if (wrap(&tm->tm_hour, 24, &tm->tm_mday) != 0) {
		goto overflow;
	}

	// In order to wrap the days of the month, we first need to know what
	// month it is
	if (wrap(&tm->tm_mon, 12, &tm->tm_year) != 0) {
		goto overflow;
	}

	if (tm->tm_mday < 1) {
		do {
			--tm->tm_mon;
			if (wrap(&tm->tm_mon, 12, &tm->tm_year) != 0) {
				goto overflow;
			}

			tm->tm_mday += month_length(tm->tm_year, tm->tm_mon);
		} while (tm->tm_mday < 1);
	} else {
		while (true) {
			int days = month_length(tm->tm_year, tm->tm_mon);
			if (tm->tm_mday <= days) {
				break;
			}

			tm->tm_mday -= days;
			++tm->tm_mon;
			if (wrap(&tm->tm_mon, 12, &tm->tm_year) != 0) {
				goto overflow;
			}
		}
	}

	tm->tm_yday = 0;
	for (int i = 0; i < tm->tm_mon; ++i) {
		tm->tm_yday += month_length(tm->tm_year, i);
	}
	tm->tm_yday += tm->tm_mday - 1;

	int leap_days;
	// Compute floor((year - 69)/4) - floor((year - 1)/100) + floor((year + 299)/400) without overflows
	if (tm->tm_year >= 0) {
		leap_days = floor_div(tm->tm_year - 69, 4) - floor_div(tm->tm_year - 1, 100) + floor_div(tm->tm_year - 101, 400) + 1;
	} else {
		leap_days = floor_div(tm->tm_year + 3, 4) - floor_div(tm->tm_year + 99, 100) + floor_div(tm->tm_year + 299, 400) - 17;
	}

	long long epoch_days = 365LL*(tm->tm_year - 70) + leap_days + tm->tm_yday;
	tm->tm_wday = (epoch_days + 4)%7;
	if (tm->tm_wday < 0) {
		tm->tm_wday += 7;
	}

	long long epoch_time = tm->tm_sec + 60*(tm->tm_min + 60*(tm->tm_hour + 24*epoch_days));
	*timep = (time_t)epoch_time;
	if ((long long)*timep != epoch_time) {
		goto overflow;
	}
	return 0;

overflow:
	errno = EOVERFLOW;
	return -1;
}

/** Parse some digits from a timestamp. */
static int parse_timestamp_part(const char **str, size_t n, int *result) {
	char buf[n + 1];
	for (size_t i = 0; i < n; ++i, ++*str) {
		char c = **str;
		if (c < '0' || c > '9') {
			return -1;
		}
		buf[i] = c;
	}
	buf[n] = '\0';

	*result = atoi(buf);
	return 0;
}

int parse_timestamp(const char *str, struct timespec *result) {
	struct tm tm = {
		.tm_isdst = -1,
	};

	int tz_hour = 0;
	int tz_min = 0;
	bool tz_negative = false;
	bool local = true;

	// YYYY
	if (parse_timestamp_part(&str, 4, &tm.tm_year) != 0) {
		goto invalid;
	}
	tm.tm_year -= 1900;

	// MM
	if (*str == '-') {
		++str;
	}
	if (parse_timestamp_part(&str, 2, &tm.tm_mon) != 0) {
		goto invalid;
	}
	tm.tm_mon -= 1;

	// DD
	if (*str == '-') {
		++str;
	}
	if (parse_timestamp_part(&str, 2, &tm.tm_mday) != 0) {
		goto invalid;
	}

	if (!*str) {
		goto end;
	} else if (*str == 'T') {
		++str;
	}

	// hh
	if (parse_timestamp_part(&str, 2, &tm.tm_hour) != 0) {
		goto invalid;
	}

	// mm
	if (!*str) {
		goto end;
	} else if (*str == ':') {
		++str;
	}
	if (parse_timestamp_part(&str, 2, &tm.tm_min) != 0) {
		goto invalid;
	}

	// ss
	if (!*str) {
		goto end;
	} else if (*str == ':') {
		++str;
	}
	if (parse_timestamp_part(&str, 2, &tm.tm_sec) != 0) {
		goto invalid;
	}

	if (!*str) {
		goto end;
	} else if (*str == 'Z') {
		local = false;
		++str;
	} else if (*str == '+' || *str == '-') {
		local = false;
		tz_negative = *str == '-';
		++str;

		// hh
		if (parse_timestamp_part(&str, 2, &tz_hour) != 0) {
			goto invalid;
		}

		// mm
		if (!*str) {
			goto end;
		} else if (*str == ':') {
			++str;
		}
		if (parse_timestamp_part(&str, 2, &tz_min) != 0) {
			goto invalid;
		}
	} else {
		goto invalid;
	}

	if (*str) {
		goto invalid;
	}

end:
	if (local) {
		if (xmktime(&tm, &result->tv_sec) != 0) {
			goto error;
		}
	} else {
		if (xtimegm(&tm, &result->tv_sec) != 0) {
			goto error;
		}

		int offset = 60*tz_hour + tz_min;
		if (tz_negative) {
			result->tv_sec -= offset;
		} else {
			result->tv_sec += offset;
		}
	}

	result->tv_nsec = 0;
	return 0;

invalid:
	errno = EINVAL;
error:
	return -1;
}
