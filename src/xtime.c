// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include "xtime.h"

#include "alloc.h"
#include "bfs.h"
#include "bfstd.h"
#include "diag.h"
#include "sanity.h"

#include <errno.h>
#include <limits.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

int xmktime(struct tm *tm, time_t *timep) {
	time_t time = mktime(tm);

	if (time == -1) {
		int error = errno;

		struct tm tmp;
		if (!localtime_r(&time, &tmp)) {
			bfs_ebug("localtime_r(-1)");
			return -1;
		}

		if (tm->tm_year != tmp.tm_year || tm->tm_yday != tmp.tm_yday
		    || tm->tm_hour != tmp.tm_hour || tm->tm_min != tmp.tm_min || tm->tm_sec != tmp.tm_sec) {
			errno = error;
			return -1;
		}
	}

	*timep = time;
	return 0;
}

// FreeBSD is missing an interceptor
#if BFS_HAS_TIMEGM && !(__FreeBSD__ && __SANITIZE_MEMORY__)

int xtimegm(struct tm *tm, time_t *timep) {
	time_t time = timegm(tm);

	if (time == -1) {
		int error = errno;

		struct tm tmp;
		if (!gmtime_r(&time, &tmp)) {
			bfs_ebug("gmtime_r(-1)");
			return -1;
		}

		if (tm->tm_year != tmp.tm_year || tm->tm_yday != tmp.tm_yday
		    || tm->tm_hour != tmp.tm_hour || tm->tm_min != tmp.tm_min || tm->tm_sec != tmp.tm_sec) {
			errno = error;
			return -1;
		}
	}

	*timep = time;
	return 0;
}

#else

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
	return (n + a) / d - a;
}

static int wrap(int *value, int max, int *next) {
	int carry = floor_div(*value, max);
	*value -= carry * max;
	return safe_add(next, carry);
}

static int month_length(int year, int month) {
	static const int month_lengths[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	int ret = month_lengths[month];
	if (month == 1 && year % 4 == 0 && (year % 100 != 0 || (year + 300) % 400 == 0)) {
		++ret;
	}
	return ret;
}

int xtimegm(struct tm *tm, time_t *timep) {
	struct tm copy = *tm;
	copy.tm_isdst = 0;

	if (wrap(&copy.tm_sec, 60, &copy.tm_min) != 0) {
		goto overflow;
	}
	if (wrap(&copy.tm_min, 60, &copy.tm_hour) != 0) {
		goto overflow;
	}
	if (wrap(&copy.tm_hour, 24, &copy.tm_mday) != 0) {
		goto overflow;
	}

	// In order to wrap the days of the month, we first need to know what
	// month it is
	if (wrap(&copy.tm_mon, 12, &copy.tm_year) != 0) {
		goto overflow;
	}

	if (copy.tm_mday < 1) {
		do {
			--copy.tm_mon;
			if (wrap(&copy.tm_mon, 12, &copy.tm_year) != 0) {
				goto overflow;
			}

			copy.tm_mday += month_length(copy.tm_year, copy.tm_mon);
		} while (copy.tm_mday < 1);
	} else {
		while (true) {
			int days = month_length(copy.tm_year, copy.tm_mon);
			if (copy.tm_mday <= days) {
				break;
			}

			copy.tm_mday -= days;
			++copy.tm_mon;
			if (wrap(&copy.tm_mon, 12, &copy.tm_year) != 0) {
				goto overflow;
			}
		}
	}

	copy.tm_yday = 0;
	for (int i = 0; i < copy.tm_mon; ++i) {
		copy.tm_yday += month_length(copy.tm_year, i);
	}
	copy.tm_yday += copy.tm_mday - 1;

	int leap_days;
	// Compute floor((year - 69)/4) - floor((year - 1)/100) + floor((year + 299)/400) without overflows
	if (copy.tm_year >= 0) {
		leap_days = floor_div(copy.tm_year - 69, 4) - floor_div(copy.tm_year - 1, 100) + floor_div(copy.tm_year - 101, 400) + 1;
	} else {
		leap_days = floor_div(copy.tm_year + 3, 4) - floor_div(copy.tm_year + 99, 100) + floor_div(copy.tm_year + 299, 400) - 17;
	}

	long long epoch_days = 365LL * (copy.tm_year - 70) + leap_days + copy.tm_yday;
	copy.tm_wday = (epoch_days + 4) % 7;
	if (copy.tm_wday < 0) {
		copy.tm_wday += 7;
	}

	long long epoch_time = copy.tm_sec + 60 * (copy.tm_min + 60 * (copy.tm_hour + 24 * epoch_days));
	time_t time = (time_t)epoch_time;
	if ((long long)time != epoch_time) {
		goto overflow;
	}

	*tm = copy;
	*timep = time;
	return 0;

overflow:
	errno = EOVERFLOW;
	return -1;
}

#endif // !BFS_HAS_TIMEGM

/** Parse a decimal digit. */
static int xgetdigit(char c) {
	int ret = c - '0';
	if (ret < 0 || ret > 9) {
		return -1;
	} else {
		return ret;
	}
}

/** Parse some digits from a timestamp. */
static int xgetpart(const char **str, size_t n, int *result) {
	*result = 0;

	for (size_t i = 0; i < n; ++i, ++*str) {
		int dig = xgetdigit(**str);
		if (dig < 0) {
			return -1;
		}
		*result *= 10;
		*result += dig;
	}

	return 0;
}

int xgetdate(const char *str, struct timespec *result) {
	// Handle @epochseconds
	if (str[0] == '@') {
		long long value;
		if (xstrtoll(str + 1, NULL, 10, &value) != 0) {
			goto error;
		}

		time_t time = (time_t)value;
		if ((long long)time != value) {
			errno = ERANGE;
			goto error;
		}

		result->tv_sec = time;
		goto done;
	}

	struct tm tm = {
		.tm_isdst = -1,
	};

	int tz_hour = 0;
	int tz_min = 0;
	bool tz_negative = false;
	bool local = true;

	// YYYY
	if (xgetpart(&str, 4, &tm.tm_year) != 0) {
		goto invalid;
	}
	tm.tm_year -= 1900;

	// MM
	if (*str == '-') {
		++str;
	}
	if (xgetpart(&str, 2, &tm.tm_mon) != 0) {
		goto invalid;
	}
	tm.tm_mon -= 1;

	// DD
	if (*str == '-') {
		++str;
	}
	if (xgetpart(&str, 2, &tm.tm_mday) != 0) {
		goto invalid;
	}

	if (!*str) {
		goto end;
	} else if (*str == 'T' || *str == ' ') {
		++str;
	}

	// hh
	if (xgetpart(&str, 2, &tm.tm_hour) != 0) {
		goto invalid;
	}

	// mm
	if (!*str) {
		goto end;
	} else if (*str == ':') {
		++str;
	} else if (xgetdigit(*str) < 0) {
		goto zone;
	}
	if (xgetpart(&str, 2, &tm.tm_min) != 0) {
		goto invalid;
	}

	// ss
	if (!*str) {
		goto end;
	} else if (*str == ':') {
		++str;
	} else if (xgetdigit(*str) < 0) {
		goto zone;
	}
	if (xgetpart(&str, 2, &tm.tm_sec) != 0) {
		goto invalid;
	}

zone:
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
		if (xgetpart(&str, 2, &tz_hour) != 0) {
			goto invalid;
		}

		// mm
		if (!*str) {
			goto end;
		} else if (*str == ':') {
			++str;
		}
		if (xgetpart(&str, 2, &tz_min) != 0) {
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

		int offset = (tz_hour * 60 + tz_min) * 60;
		if (tz_negative) {
			result->tv_sec += offset;
		} else {
			result->tv_sec -= offset;
		}
	}

done:
	result->tv_nsec = 0;
	return 0;

invalid:
	errno = EINVAL;
error:
	return -1;
}

/** One nanosecond. */
static const long NS = 1000L * 1000 * 1000;

void timespec_add(struct timespec *lhs, const struct timespec *rhs) {
	lhs->tv_sec += rhs->tv_sec;
	lhs->tv_nsec += rhs->tv_nsec;
	if (lhs->tv_nsec >= NS) {
		lhs->tv_nsec -= NS;
		lhs->tv_sec += 1;
	}
}

void timespec_sub(struct timespec *lhs, const struct timespec *rhs) {
	lhs->tv_sec -= rhs->tv_sec;
	lhs->tv_nsec -= rhs->tv_nsec;
	if (lhs->tv_nsec < 0) {
		lhs->tv_nsec += NS;
		lhs->tv_sec -= 1;
	}
}

int timespec_cmp(const struct timespec *lhs, const struct timespec *rhs) {
	if (lhs->tv_sec < rhs->tv_sec) {
		return -1;
	} else if (lhs->tv_sec > rhs->tv_sec) {
		return 1;
	}

	if (lhs->tv_nsec < rhs->tv_nsec) {
		return -1;
	} else if (lhs->tv_nsec > rhs->tv_nsec) {
		return 1;
	}

	return 0;
}

void timespec_min(struct timespec *dest, const struct timespec *src) {
	if (timespec_cmp(src, dest) < 0) {
		*dest = *src;
	}
}

void timespec_max(struct timespec *dest, const struct timespec *src) {
	if (timespec_cmp(src, dest) > 0) {
		*dest = *src;
	}
}

double timespec_ns(const struct timespec *ts) {
	return 1.0e9 * ts->tv_sec + ts->tv_nsec;
}

#if defined(_POSIX_TIMERS) && BFS_HAS_TIMER_CREATE
#  define BFS_POSIX_TIMERS _POSIX_TIMERS
#else
#  define BFS_POSIX_TIMERS (-1)
#endif

struct timer {
#if BFS_POSIX_TIMERS >= 0
	/** The POSIX timer. */
	timer_t timer;
#endif
	/** Whether to use timer_create() or setitimer(). */
	bool legacy;
};

struct timer *xtimer_start(const struct timespec *interval) {
	struct timer *timer = ALLOC(struct timer);
	if (!timer) {
		return NULL;
	}

#if BFS_POSIX_TIMERS >= 0
	if (sysoption(TIMERS)) {
		clockid_t clock = CLOCK_REALTIME;

#if defined(_POSIX_MONOTONIC_CLOCK) && _POSIX_MONOTONIC_CLOCK >= 0
		if (sysoption(MONOTONIC_CLOCK) > 0) {
			clock = CLOCK_MONOTONIC;
		}
#endif

		if (timer_create(clock, NULL, &timer->timer) != 0) {
			goto fail;
		}

		// https://github.com/llvm/llvm-project/issues/111847
		sanitize_init(&timer->timer);

		struct itimerspec spec = {
			.it_value = *interval,
			.it_interval = *interval,
		};
		if (timer_settime(timer->timer, 0, &spec, NULL) != 0) {
			timer_delete(timer->timer);
			goto fail;
		}

		timer->legacy = false;
		return timer;
	}
#endif

#if BFS_POSIX_TIMERS <= 0
	struct timeval tv = {
		.tv_sec = interval->tv_sec,
		.tv_usec = (interval->tv_nsec + 999) / 1000,
	};
	struct itimerval ival = {
		.it_value = tv,
		.it_interval = tv,
	};
	if (setitimer(ITIMER_REAL, &ival, NULL) != 0) {
		goto fail;
	}

	timer->legacy = true;
	return timer;
#endif

fail:
	free(timer);
	return NULL;
}

void xtimer_stop(struct timer *timer) {
	if (!timer) {
		return;
	}

	if (timer->legacy) {
#if BFS_POSIX_TIMERS <= 0
		struct itimerval ival = {0};
		int ret = setitimer(ITIMER_REAL, &ival, NULL);
		bfs_everify(ret == 0, "setitimer()");
#endif
	} else {
#if BFS_POSIX_TIMERS >= 0
		int ret = timer_delete(timer->timer);
		bfs_everify(ret == 0, "timer_delete()");
#endif
	}

	free(timer);
}
