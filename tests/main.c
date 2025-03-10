// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

/**
 * Entry point for unit tests.
 */

#include "tests.h"

#include "alloc.h"
#include "bfstd.h"
#include "color.h"
#include "list.h"

#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/** Result of the current test. */
static bool pass;

bool bfs_check_impl(bool result) {
	pass &= result;
	return result;
}

/**
 * A running test.
 */
struct test_proc {
	/** Linked list links. */
	struct test_proc *prev, *next;

	/** The PID of this test. */
	pid_t pid;
	/** The name of this test. */
	const char *name;
};

/**
 * Global test context.
 */
struct test_ctx {
	/** Number of command line arguments. */
	int argc;
	/** The arguments themselves. */
	char **argv;

	/** Maximum jobs (-j). */
	int jobs;
	/** Current jobs. */
	int running;
	/** Completed jobs. */
	int done;
	/** List of running tests. */
	struct {
		struct test_proc *head, *tail;
	} procs;

	/** Parsed colors. */
	struct colors *colors;
	/** Colorized output stream. */
	CFILE *cout;

	/** Eventual exit status. */
	int ret;
};

/** Initialize the test context. */
static int test_init(struct test_ctx *ctx, int jobs, int argc, char **argv) {
	ctx->argc = argc;
	ctx->argv = argv;

	ctx->jobs = jobs;
	ctx->running = 0;
	ctx->done = 0;
	LIST_INIT(&ctx->procs);

	ctx->colors = parse_colors();
	ctx->cout = cfwrap(stdout, ctx->colors, false);
	if (!ctx->cout) {
		ctx->ret = EXIT_FAILURE;
		return -1;
	}

	ctx->ret = EXIT_SUCCESS;
	return 0;
}

/** Check if a test case is enabled for this run. */
static bool should_run(const struct test_ctx *ctx, const char *test) {
	// Run all tests by default
	if (ctx->argc == 0) {
		return true;
	}

	// With args, run only specified tests
	for (int i = 0; i < ctx->argc; ++i) {
		if (strcmp(test, ctx->argv[i]) == 0) {
			return true;
		}
	}

	return false;
}

/** Wait for a test to finish. */
static void wait_test(struct test_ctx *ctx) {
	int wstatus;
	pid_t pid = xwaitpid(0, &wstatus, 0);
	bfs_everify(pid > 0, "xwaitpid()");

	struct test_proc *proc = NULL;
	for_list (struct test_proc, i, &ctx->procs) {
		if (i->pid == pid) {
			proc = i;
			break;
		}
	}

	bfs_verify(proc, "No test_proc for PID %ju", (intmax_t)pid);

	bool passed = false;

	if (WIFEXITED(wstatus)) {
		int status = WEXITSTATUS(wstatus);
		if (status == EXIT_SUCCESS) {
			cfprintf(ctx->cout, "${grn}[PASS]${rs} ${bld}%s${rs}\n", proc->name);
			passed = true;
		} else if (status == EXIT_FAILURE) {
			cfprintf(ctx->cout, "${red}[FAIL]${rs} ${bld}%s${rs}\n", proc->name);
		} else {
			cfprintf(ctx->cout, "${red}[FAIL]${rs} ${bld}%s${rs} (Exit %d)\n", proc->name, status);
		}
	} else {
		const char *str = NULL;
		if (WIFSIGNALED(wstatus)) {
			str = strsignal(WTERMSIG(wstatus));
		}
		if (!str) {
			str = "Unknown";
		}
		cfprintf(ctx->cout, "${red}[FAIL]${rs} ${bld}%s${rs} (%s)\n", proc->name, str);
	}

	if (!passed) {
		ctx->ret = EXIT_FAILURE;
	}

	--ctx->running;
	++ctx->done;
	LIST_REMOVE(&ctx->procs, proc);
	free(proc);
}

/** Unit test function type. */
typedef void test_fn(void);

/** Run a test if it's enabled. */
static void run_test(struct test_ctx *ctx, const char *test, test_fn *fn) {
	if (!should_run(ctx, test)) {
		return;
	}

	while (ctx->running >= ctx->jobs) {
		wait_test(ctx);
	}

	struct test_proc *proc = ALLOC(struct test_proc);
	bfs_everify(proc, "alloc()");

	LIST_ITEM_INIT(proc);
	proc->name = test;

	fflush(NULL);
	proc->pid = fork();
	bfs_everify(proc->pid >= 0, "fork()");

	if (proc->pid > 0) {
		// Parent
		++ctx->running;
		LIST_APPEND(&ctx->procs, proc);
		return;
	}

	// Child
	pass = true;
	fn();
	exit(pass ? EXIT_SUCCESS : EXIT_FAILURE);
}

/** Finalize the test context. */
static int test_fini(struct test_ctx *ctx) {
	while (ctx->running > 0) {
		wait_test(ctx);
	}

	if (ctx->cout) {
		cfclose(ctx->cout);
	}

	free_colors(ctx->colors);

	return ctx->ret;
}

int main(int argc, char *argv[]) {
	// Try to set a UTF-8 locale
	if (!setlocale(LC_ALL, "C.UTF-8")) {
		setlocale(LC_ALL, "");
	}

	// Run tests in UTC
	if (setenv("TZ", "UTC0", true) != 0) {
		perror("setenv()");
		return EXIT_FAILURE;
	}
	tzset();

	long jobs = 0;

	const char *cmd = argc > 0 ? argv[0] : "units";
	int c;
	while (c = getopt(argc, argv, ":j:"), c != -1) {
		switch (c) {
		case 'j':
			if (xstrtol(optarg, NULL, 10, &jobs) != 0 || jobs <= 0) {
				fprintf(stderr, "%s: Bad job count '%s'\n", cmd, optarg);
				return EXIT_FAILURE;
			}
			break;
		case ':':
			fprintf(stderr, "%s: Missing argument to -%c\n", cmd, optopt);
			return EXIT_FAILURE;
		case '?':
			fprintf(stderr, "%s: Unrecognized option -%c\n", cmd, optopt);
			return EXIT_FAILURE;
		}
	}

	if (jobs == 0) {
		jobs = nproc();
	}

	if (optind > argc) {
		optind = argc;
	}

	struct test_ctx ctx;
	if (test_init(&ctx, jobs, argc - optind, argv + optind) != 0) {
		goto done;
	}

	run_test(&ctx, "alloc", check_alloc);
	run_test(&ctx, "bfstd", check_bfstd);
	run_test(&ctx, "bit", check_bit);
	run_test(&ctx, "ioq", check_ioq);
	run_test(&ctx, "list", check_list);
	run_test(&ctx, "sighook", check_sighook);
	run_test(&ctx, "trie", check_trie);
	run_test(&ctx, "xspawn", check_xspawn);
	run_test(&ctx, "xtime", check_xtime);

done:
	return test_fini(&ctx);
}
