// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include "atomic.h"
#include "bfs.h"
#include "bfstd.h"
#include "diag.h"
#include "ioq.h"
#include "sighook.h"
#include "xtime.h"

#include <errno.h>
#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

/** Which clock to use for benchmarking. */
static clockid_t clockid = CLOCK_REALTIME;

/** Get a current time measurement. */
static void gettime(struct timespec *tp) {
	int ret = clock_gettime(clockid, tp);
	bfs_everify(ret == 0, "clock_gettime(%d)", (int)clockid);
}

/**
 * Time measurements.
 */
struct times {
	/** The start time. */
	struct timespec start;

	/** Total requests started. */
	size_t pushed;
	/** Total requests finished. */
	size_t popped;

	/** Number of timed requests (latency). */
	size_t timed_reqs;
	/** The start time for the currently tracked request. */
	struct timespec req_start;
	/** Whether a timed request is currently in flight. */
	bool timing;

	/** Latency measurements. */
	struct {
		struct timespec min;
		struct timespec max;
		struct timespec sum;
	} latency;
};

/** Initialize a timer. */
static void times_init(struct times *times) {
	*times = (struct times) {
		.latency = {
			.min = { .tv_sec = 1000 },
		},
	};
	gettime(&times->start);
}

/** Start timing a single request. */
static void start_request(struct times *times) {
	gettime(&times->req_start);
	times->timing = true;
}

/** Finish timing a request. */
static void finish_request(struct times *times) {
	struct timespec elapsed;
	gettime(&elapsed);
	timespec_sub(&elapsed, &times->req_start);

	timespec_min(&times->latency.min, &elapsed);
	timespec_max(&times->latency.max, &elapsed);
	timespec_add(&times->latency.sum, &elapsed);

	bfs_assert(times->timing);
	times->timing = false;
	++times->timed_reqs;
}

/** Add times to the totals, and reset the lap times. */
static void times_lap(struct times *total, struct times *lap) {
	total->pushed += lap->pushed;
	total->popped += lap->popped;
	total->timed_reqs += lap->timed_reqs;

	timespec_min(&total->latency.min, &lap->latency.min);
	timespec_max(&total->latency.max, &lap->latency.max);
	timespec_add(&total->latency.sum, &lap->latency.sum);

	times_init(lap);
}

/** Print some times. */
static void times_print(const struct times *times, long seconds) {
	struct timespec elapsed;
	gettime(&elapsed);
	timespec_sub(&elapsed, &times->start);

	double fsec = timespec_ns(&elapsed) / 1.0e9;
	double iops = times->popped / fsec;
	double mean = timespec_ns(&times->latency.sum) / times->timed_reqs;
	double min = timespec_ns(&times->latency.min);
	double max = timespec_ns(&times->latency.max);

	if (seconds > 0) {
		printf("%9ld", seconds);
	} else if (elapsed.tv_nsec >= 10 * 1000 * 1000) {
		printf("%9.2f", fsec);
	} else {
		printf("%9.0f", fsec);
	}

	printf(" │ %'17.0f │ %'15.0f ∈ [%'6.0f .. %'7.0f]\n", iops, mean, min, max);
	fflush(stdout);
}

/** Push an ioq request. */
static bool push(struct ioq *ioq, enum ioq_nop_type type, struct times *lap) {
	void *ptr = NULL;

	// Track latency for a small fraction of requests
	if (!lap->timing && (lap->pushed + 1) % 128 == 0) {
		start_request(lap);
		ptr = lap;
	}

	int ret = ioq_nop(ioq, type, ptr);
	if (ret != 0) {
		bfs_everify(errno == EAGAIN, "ioq_nop(%d)", (int)type);
		return false;
	}

	++lap->pushed;
	return true;
}

/** Pop an ioq request. */
static bool pop(struct ioq *ioq, struct times *lap, bool block) {
	struct ioq_ent *ent = ioq_pop(ioq, block);
	if (!ent) {
		return false;
	}

	if (ent->ptr) {
		finish_request(lap);
	}

	ioq_free(ioq, ent);
	++lap->popped;
	return true;
}

/** ^C flag. */
static atomic bool quit = false;

/** ^C hook. */
static void ctrlc(int sig, siginfo_t *info, void *arg) {
	store(&quit, true, relaxed);
}

int main(int argc, char *argv[]) {
	// Use CLOCK_MONOTONIC if available
#if defined(_POSIX_MONOTONIC_CLOCK) && _POSIX_MONOTONIC_CLOCK >= 0
	if (sysoption(MONOTONIC_CLOCK) > 0) {
		clockid = CLOCK_MONOTONIC;
	}
#endif

	// Enable thousands separators
	setlocale(LC_ALL, "");

	// -d: queue depth
	long depth = 4096;
	// -j: threads
	long threads = 0;
	// -t: timeout
	double timeout = 5.0;
	// -L|-H: ioq_nop() type
	enum ioq_nop_type type = IOQ_NOP_LIGHT;

	const char *cmd = argc > 0 ? argv[0] : "ioq";
	int c;
	while (c = getopt(argc, argv, ":d:j:t:LH"), c != -1) {
		switch (c) {
		case 'd':
			if (xstrtol(optarg, NULL, 10, &depth) != 0) {
				fprintf(stderr, "%s: Bad depth '%s': %s\n", cmd, optarg, errstr());
				return EXIT_FAILURE;
			}
			break;
		case 'j':
			if (xstrtol(optarg, NULL, 10, &threads) != 0) {
				fprintf(stderr, "%s: Bad thread count '%s': %s\n", cmd, optarg, errstr());
				return EXIT_FAILURE;
			}
			break;
		case 't':
			if (xstrtod(optarg, NULL, &timeout) != 0) {
				fprintf(stderr, "%s: Bad timeout '%s': %s\n", cmd, optarg, errstr());
				return EXIT_FAILURE;
			}
			break;
		case 'L':
		 	type = IOQ_NOP_LIGHT;
			break;
		case 'H':
		 	type = IOQ_NOP_HEAVY;
			break;
		case ':':
			fprintf(stderr, "%s: Missing argument to -%c\n", cmd, optopt);
			return EXIT_FAILURE;
		case '?':
			fprintf(stderr, "%s: Unrecognized option -%c\n", cmd, optopt);
			return EXIT_FAILURE;
		}
	}

	if (threads <= 0) {
		threads = nproc();
		if (threads > 8) {
			threads = 8;
		}
	}
	if (threads < 2) {
		threads = 2;
	}
	--threads;

	// Listen for ^C to print the summary
	struct sighook *hook = sighook(SIGINT, ctrlc, NULL, SH_CONTINUE | SH_ONESHOT);

	printf("I/O queue benchmark (%s)\n\n", bfs_version);

	printf("[-d] depth:   %ld\n", depth);
	printf("[-j] threads: %ld (including main)\n", threads + 1);
	if (type == IOQ_NOP_HEAVY) {
		printf("[-H] type:    heavy (with syscalls)\n");
	} else {
		printf("[-L] type:    light (no syscalls)\n");
	}
	printf("\n");

	printf(" Time (s) │ Throughput (IO/s) │ Latency (ns/IO)\n");
	printf("══════════╪═══════════════════╪═════════════════\n");
	fflush(stdout);

	struct ioq *ioq = ioq_create(depth, threads);
	bfs_everify(ioq, "ioq_create(%ld, %ld)", depth, threads);

	// Pre-allocate all the requests
	while (ioq_capacity(ioq) > 0) {
		int ret = ioq_nop(ioq, type, NULL);
		bfs_everify(ret == 0, "ioq_nop(%d)", (int)type);
	}
	while (true) {
		struct ioq_ent *ent = ioq_pop(ioq, true);
		if (!ent) {
			break;
		}
		ioq_free(ioq, ent);
	}

	struct times total, lap;
	times_init(&total);
	lap = total;

	long seconds = 0;
	while (!load(&quit, relaxed)) {
		bool was_timing = lap.timing;

		for (int i = 0; i < 16; ++i) {
			bool block = ioq_capacity(ioq) == 0;
			if (!pop(ioq, &lap, block)) {
				break;
			}
		}

		if (was_timing && !lap.timing) {
			struct timespec elapsed;
			gettime(&elapsed);
			timespec_sub(&elapsed, &total.start);

			if (elapsed.tv_sec > seconds) {
				seconds = elapsed.tv_sec;
				times_print(&lap, seconds);
				times_lap(&total, &lap);
			}

			double ns = timespec_ns(&elapsed);
			if (timeout > 0 && ns >= timeout * 1.0e9) {
				break;
			}
		}

		for (int i = 0; i < 8; ++i) {
			if (!push(ioq, type, &lap)) {
				break;
			}
		}
		ioq_submit(ioq);
	}

	while (pop(ioq, &lap, true));
	times_lap(&total, &lap);

	if (load(&quit, relaxed)) {
		printf("\r────^C────┼───────────────────┼─────────────────\n");
	} else {
		printf("──────────┼───────────────────┼─────────────────\n");
	}
	times_print(&total, 0);

	ioq_destroy(ioq);
	sigunhook(hook);
	return 0;
}
