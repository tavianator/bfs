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
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
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

int xtimegm(struct tm *tm, time_t *timep) {
	// Some man pages for timegm() recommend this as a portable approach
	int ret = -1;
	int error;

	char *old_tz = getenv("TZ");
	if (old_tz) {
		old_tz = strdup(old_tz);
		if (!old_tz) {
			error = errno;
			goto fail;
		}
	}

	if (setenv("TZ", "UTC0", true) != 0) {
		error = errno;
		goto fail;
	}

	ret = xmktime(tm, timep);
	error = errno;

	if (old_tz) {
		if (setenv("TZ", old_tz, true) != 0) {
			ret = -1;
			error = errno;
			goto fail;
		}
	} else {
		if (unsetenv("TZ") != 0) {
			ret = -1;
			error = errno;
			goto fail;
		}
	}

	tzset();
fail:
	free(old_tz);
	errno = error;
	return ret;
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
