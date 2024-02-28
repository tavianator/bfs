// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include "tests.h"
#include "../src/xtime.h"
#include "../src/bfstd.h"
#include "../src/config.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static bool tm_equal(const struct tm *tma, const struct tm *tmb) {
	if (tma->tm_year != tmb->tm_year) {
		return false;
	}
	if (tma->tm_mon != tmb->tm_mon) {
		return false;
	}
	if (tma->tm_mday != tmb->tm_mday) {
		return false;
	}
	if (tma->tm_hour != tmb->tm_hour) {
		return false;
	}
	if (tma->tm_min != tmb->tm_min) {
		return false;
	}
	if (tma->tm_sec != tmb->tm_sec) {
		return false;
	}
	if (tma->tm_wday != tmb->tm_wday) {
		return false;
	}
	if (tma->tm_yday != tmb->tm_yday) {
		return false;
	}
	if (tma->tm_isdst != tmb->tm_isdst) {
		return false;
	}

	return true;
}

static void tm_print(FILE *file, const struct tm *tm) {
	fprintf(file, "Y%d M%d D%d  h%d m%d s%d  wd%d yd%d%s\n",
		tm->tm_year, tm->tm_mon, tm->tm_mday,
		tm->tm_hour, tm->tm_min, tm->tm_sec,
		tm->tm_wday, tm->tm_yday,
		tm->tm_isdst ? (tm->tm_isdst < 0 ? " (DST?)" : " (DST)") : "");
}

/** Check one xgetdate() result. */
static bool compare_xgetdate(const char *str, int error, time_t expected) {
	struct timespec ts;
	int ret = xgetdate(str, &ts);

	if (error) {
		if (ret != -1 || errno != error) {
			fprintf(stderr, "xgetdate('%s'): %s\n", str, xstrerror(errno));
			return false;
		}
	} else if (ret != 0) {
		fprintf(stderr, "xgetdate('%s'): %s\n", str, xstrerror(errno));
		return false;
	} else if (ts.tv_sec != expected || ts.tv_nsec != 0) {
		fprintf(stderr, "xgetdate('%s'): %jd.%09jd != %jd\n", str, (intmax_t)ts.tv_sec, (intmax_t)ts.tv_nsec, (intmax_t)expected);
		return false;
	}

	return true;
}

/** xgetdate() tests. */
static bool check_xgetdate(void) {
	return compare_xgetdate("", EINVAL, 0)
		& compare_xgetdate("????", EINVAL, 0)
		& compare_xgetdate("1991", EINVAL, 0)
		& compare_xgetdate("1991-??", EINVAL, 0)
		& compare_xgetdate("1991-12", EINVAL, 0)
		& compare_xgetdate("1991-12-", EINVAL, 0)
		& compare_xgetdate("1991-12-??", EINVAL, 0)
		& compare_xgetdate("1991-12-14", 0, 692668800)
		& compare_xgetdate("1991-12-14-", EINVAL, 0)
		& compare_xgetdate("1991-12-14T", EINVAL, 0)
		& compare_xgetdate("1991-12-14T??", EINVAL, 0)
		& compare_xgetdate("1991-12-14T10", 0, 692704800)
		& compare_xgetdate("1991-12-14T10:??", EINVAL, 0)
		& compare_xgetdate("1991-12-14T10:11", 0, 692705460)
		& compare_xgetdate("1991-12-14T10:11:??", EINVAL, 0)
		& compare_xgetdate("1991-12-14T10:11:12", 0, 692705472)
		& compare_xgetdate("1991-12-14T10Z", 0, 692704800)
		& compare_xgetdate("1991-12-14T10:11Z", 0, 692705460)
		& compare_xgetdate("1991-12-14T10:11:12Z", 0, 692705472)
		& compare_xgetdate("1991-12-14T10:11:12?", EINVAL, 0)
		& compare_xgetdate("1991-12-14T02-08", 0, 692704800)
		& compare_xgetdate("1991-12-14T06:41-03:30", 0, 692705460)
		& compare_xgetdate("1991-12-14T02:11:12-08:00", 0, 692705472)
		& compare_xgetdate("19911214 021112-0800", 0, 692705472);
}

/** xmktime() tests. */
static bool check_xmktime(void) {
	for (time_t time = -10; time <= 10; ++time) {
		struct tm tm;
		if (xlocaltime(&time, &tm) != 0) {
			perror("xlocaltime()");
			return false;
		}

		time_t made;
		if (xmktime(&tm, &made) != 0) {
			perror("xmktime()");
			return false;
		}

		if (time != made) {
			fprintf(stderr, "Mismatch: %jd != %jd\n", (intmax_t)time, (intmax_t)made);
			tm_print(stderr, &tm);
			return false;
		}
	}

	return true;
}

/** xtimegm() tests. */
static bool check_xtimegm(void) {
	struct tm tm = {
		.tm_isdst = -1,
	};

	for (tm.tm_year =  10; tm.tm_year <= 200; tm.tm_year += 10)
	for (tm.tm_mon  =  -3; tm.tm_mon  <=  15; tm.tm_mon  +=  3)
	for (tm.tm_mday = -31; tm.tm_mday <=  61; tm.tm_mday +=  4)
	for (tm.tm_hour =  -1; tm.tm_hour <=  24; tm.tm_hour +=  5)
	for (tm.tm_min  =  -1; tm.tm_min  <=  60; tm.tm_min  += 31)
	for (tm.tm_sec  = -60; tm.tm_sec  <= 120; tm.tm_sec  +=  5) {
		struct tm tma = tm, tmb = tm;
		time_t ta, tb;
		ta = mktime(&tma);
		if (xtimegm(&tmb, &tb) != 0) {
			tb = -1;
		}

		bool fail = false;
		if (ta != tb) {
			printf("Mismatch:  %jd != %jd\n", (intmax_t)ta, (intmax_t)tb);
			fail = true;
		}
		if (ta != -1 && !tm_equal(&tma, &tmb)) {
			printf("mktime():  ");
			tm_print(stdout, &tma);
			printf("xtimegm(): ");
			tm_print(stdout, &tmb);
			fail = true;
		}
		if (fail) {
			printf("Input:     ");
			tm_print(stdout, &tm);
			return false;
		}
	}

	return true;
}

bool check_xtime(void) {
	if (setenv("TZ", "UTC0", true) != 0) {
		perror("setenv()");
		return false;
	}
	tzset();

	return check_xgetdate()
		& check_xmktime()
		& check_xtimegm();
}
