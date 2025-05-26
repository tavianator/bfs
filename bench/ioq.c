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

/** A latency sample. */
struct lat {
	/** The sampled latency. */
	struct timespec time;
	/** A random integer, for reservoir sampling. */
	long key;
};

/** Number of latency samples to keep. */
#define SAMPLES 1000
/** Latency sampling period. */
#define PERIOD 128

/** Latency measurements. */
struct lats {
	/** Lowest observed latency. */
	struct timespec min;
	/** Highest observed latency. */
	struct timespec max;
	/** Total latency. */
	struct timespec sum;
	/** Number of measured requests. */
	size_t count;

	/** Priority queue for reservoir sampling. */
	struct lat heap[SAMPLES];
	/** Current size of the heap. */
	size_t heap_size;
};

/** Initialize a latency reservoir. */
static void lats_init(struct lats *lats) {
	lats->min = (struct timespec) { .tv_sec = 1000 };
	lats->max = (struct timespec) { 0 };
	lats->sum = (struct timespec) { 0 };
	lats->count = 0;
	lats->heap_size = 0;
}

/** Binary heap parent. */
static size_t heap_parent(size_t i) {
	return (i - 1) / 2;
}

/** Binary heap left child. */
static size_t heap_child(size_t i) {
	return 2 * i + 1;
}

/** Binary heap smallest child. */
static size_t heap_min_child(const struct lats *lats, size_t i) {
	size_t j = heap_child(i);
	size_t k = j + 1;
	if (k < lats->heap_size && lats->heap[k].key < lats->heap[j].key) {
		return k;
	} else {
		return j;
	}
}

/** Check if the heap property is met. */
static bool heap_check(const struct lat *parent, const struct lat *child) {
	return parent->key <= child->key;
}

/** Reservoir sampling. */
static void heap_push(struct lats *lats, const struct lat *lat) {
	size_t i;

	if (lats->heap_size < SAMPLES) {
		// Heapify up
		i = lats->heap_size++;
		while (i > 0) {
			size_t j = heap_parent(i);
			if (heap_check(&lats->heap[j], lat)) {
				break;
			}
			lats->heap[i] = lats->heap[j];
			i = j;
		}
	} else if (lat->key > lats->heap[0].key) {
		// Heapify down
		i = 0;
		while (true) {
			size_t j = heap_min_child(lats, i);
			if (j >= SAMPLES || heap_check(lat, &lats->heap[j])) {
				break;
			}
			lats->heap[i] = lats->heap[j];
			i = j;
		}
	} else {
		// Reject
		return;
	}

	lats->heap[i] = *lat;
}

/** Add a latency sample. */
static void lats_push(struct lats *lats, const struct timespec *ts) {
	timespec_min(&lats->min, ts);
	timespec_max(&lats->max, ts);
	timespec_add(&lats->sum, ts);
	++lats->count;

	struct lat lat = {
		.time = *ts,
		.key = lrand48(),
	};
	heap_push(lats, &lat);
}

/** Merge two latency reservoirs. */
static void lats_merge(struct lats *into, const struct lats *from) {
	timespec_min(&into->min, &from->min);
	timespec_max(&into->max, &from->max);
	timespec_add(&into->sum, &from->sum);
	into->count += from->count;

	for (size_t i = 0; i < from->heap_size; ++i) {
		heap_push(into, &from->heap[i]);
	}
}

/** Latency qsort() comparator. */
static int lat_cmp(const void *a, const void *b) {
	const struct lat *la = a;
	const struct lat *lb = b;
	return timespec_cmp(&la->time, &lb->time);
}

/** Sort the latency reservoir. */
static void lats_sort(struct lats *lats) {
	qsort(lats->heap, lats->heap_size, sizeof(lats->heap[0]), lat_cmp);
}

/** Get the nth percentile. */
static const struct timespec *lats_percentile(const struct lats *lats, int percent) {
	size_t i = lats->heap_size * percent / 100;
	return &lats->heap[i].time;
}

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

	/** The start time for the currently tracked request. */
	struct timespec req_start;
	/** Whether a timed request is currently in flight. */
	bool timing;

	/** Latency measurements. */
	struct lats lats;
};

/** Initialize a timer. */
static void times_init(struct times *times) {
	gettime(&times->start);
	times->pushed = 0;
	times->popped = 0;
	bfs_assert(!times->timing);
	lats_init(&times->lats);
}

/** Finish timing a request. */
static void track_latency(struct times *times) {
	struct timespec elapsed;
	gettime(&elapsed);
	timespec_sub(&elapsed, &times->req_start);
	lats_push(&times->lats, &elapsed);

	bfs_assert(times->timing);
	times->timing = false;
}

/** Add times to the totals, and reset the lap times. */
static void times_lap(struct times *total, struct times *lap) {
	total->pushed += lap->pushed;
	total->popped += lap->popped;
	lats_merge(&total->lats, &lap->lats);

	times_init(lap);
}

/** Print some times. */
static void times_print(struct times *times, long seconds) {
	struct timespec elapsed;
	gettime(&elapsed);
	timespec_sub(&elapsed, &times->start);

	double fsec = timespec_ns(&elapsed) / 1.0e9;

	if (seconds > 0) {
		printf("%5ld", seconds);
	} else if (elapsed.tv_nsec >= 10 * 1000 * 1000) {
		printf("%5.2f", fsec);
	} else {
		printf("%5.0f", fsec);
	}

	double iops = times->popped / fsec;
	double mean = timespec_ns(&times->lats.sum) / times->lats.count;
	double min = timespec_ns(&times->lats.min);
	double max = timespec_ns(&times->lats.max);

	lats_sort(&times->lats);
	double n50 = timespec_ns(lats_percentile(&times->lats, 50));
	double n90 = timespec_ns(lats_percentile(&times->lats, 90));
	double n99 = timespec_ns(lats_percentile(&times->lats, 99));

	printf(" │ %'12.0f │ %'7.0f │ %'7.0f │ %'7.0f │ %'7.0f │ %'7.0f │ %'7.0f\n", iops, mean, min, n50, n90, n99, max);
	fflush(stdout);
}

/** Push an ioq request. */
static bool push(struct ioq *ioq, enum ioq_nop_type type, struct times *lap) {
	void *ptr = NULL;

	// Track latency for a small fraction of requests
	if (!lap->timing && (lap->pushed + 1) % PERIOD == 0) {
		ptr = lap;
		gettime(&lap->req_start);
	}

	int ret = ioq_nop(ioq, type, ptr);
	if (ret != 0) {
		bfs_everify(errno == EAGAIN, "ioq_nop(%d)", (int)type);
		return false;
	}

	++lap->pushed;
	if (ptr) {
		lap->timing = true;
	}
	return true;
}

/** Pop an ioq request. */
static bool pop(struct ioq *ioq, struct times *lap, bool block) {
	struct ioq_ent *ent = ioq_pop(ioq, block);
	if (!ent) {
		return false;
	}

	if (ent->ptr) {
		track_latency(lap);
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
	unsigned int depth = 4096;
	// -j: threads
	unsigned int threads = 0;
	// -t: timeout
	double timeout = 5.0;
	// -L|-H: ioq_nop() type
	enum ioq_nop_type type = IOQ_NOP_LIGHT;

	const char *cmd = argc > 0 ? argv[0] : "ioq";
	int c;
	while (c = getopt(argc, argv, ":d:j:t:LH"), c != -1) {
		switch (c) {
		case 'd':
			if (xstrtoui(optarg, NULL, 10, &depth) != 0) {
				fprintf(stderr, "%s: Bad depth '%s': %s\n", cmd, optarg, errstr());
				return EXIT_FAILURE;
			}
			break;
		case 'j':
			if (xstrtoui(optarg, NULL, 10, &threads) != 0) {
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

	if (!threads) {
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

	printf("[-d] depth:   %u\n", depth);
	printf("[-j] threads: %u (including main)\n", threads + 1);
	if (type == IOQ_NOP_HEAVY) {
		printf("[-H] type:    heavy (with syscalls)\n");
	} else {
		printf("[-L] type:    light (no syscalls)\n");
	}
	printf("\n");

	printf(" Time │  Throughput  │ Latency │   min   │   50%%   │   90%%   │   99%%   │   max\n");
	printf("  (s) │    (IO/s)    │ (ns/IO) │         │         │         │         │\n");
	printf("══════╪══════════════╪═════════╪═════════╪═════════╪═════════╪═════════╪═════════\n");
	fflush(stdout);

	struct ioq *ioq = ioq_create(depth, threads);
	bfs_everify(ioq, "ioq_create(%u, %u)", depth, threads);

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
		printf("\r──^C──┼──────────────┼─────────┼─────────┼─────────┼─────────┼─────────┼─────────\n");
	} else {
		printf("──────┼──────────────┼─────────┼─────────┼─────────┼─────────┼─────────┼─────────\n");
	}
	times_print(&total, 0);

	ioq_destroy(ioq);
	sigunhook(hook);
	return 0;
}
