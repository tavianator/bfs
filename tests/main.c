// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

/**
 * Entry point for unit tests.
 */

#include "prelude.h"
#include "tests.h"

#include "bfstd.h"
#include "color.h"

#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/** Result of the current test. */
static thread_local bool pass;

bool bfs_check_impl(bool result) {
	pass &= result;
	return result;
}

/** Unit test function type. */
typedef void test_fn(void);

/**
 * Global test context.
 */
struct test_ctx {
	/** Number of command line arguments. */
	int argc;
	/** The arguments themselves. */
	char **argv;

	/** Parsed colors. */
	struct colors *colors;
	/** Colorized output stream. */
	CFILE *cout;

	/** Eventual exit status. */
	int ret;
};

/** Initialize the test context. */
static int test_init(struct test_ctx *ctx, int argc, char **argv) {
	ctx->argc = argc;
	ctx->argv = argv;

	ctx->colors = parse_colors();
	ctx->cout = cfwrap(stdout, ctx->colors, false);
	if (!ctx->cout) {
		ctx->ret = EXIT_FAILURE;
		return -1;
	}

	ctx->ret = EXIT_SUCCESS;
	return 0;
}

/** Finalize the test context. */
static int test_fini(struct test_ctx *ctx) {
	if (ctx->cout) {
		cfclose(ctx->cout);
	}

	free_colors(ctx->colors);

	return ctx->ret;
}

/** Check if a test case is enabled for this run. */
static bool should_run(const struct test_ctx *ctx, const char *test) {
	// Run all tests by default
	if (ctx->argc < 2) {
		return true;
	}

	// With args, run only specified tests
	for (int i = 1; i < ctx->argc; ++i) {
		if (strcmp(test, ctx->argv[i]) == 0) {
			return true;
		}
	}

	return false;
}

/** Run a test if it's enabled. */
static void run_test(struct test_ctx *ctx, const char *test, test_fn *fn) {
	if (should_run(ctx, test)) {
		pass = true;
		fn();

		if (pass) {
			cfprintf(ctx->cout, "${grn}[PASS]${rs} ${bld}%s${rs}\n", test);
		} else {
			cfprintf(ctx->cout, "${red}[FAIL]${rs} ${bld}%s${rs}\n", test);
			ctx->ret = EXIT_FAILURE;
		}
	}
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

	struct test_ctx ctx;
	if (test_init(&ctx, argc, argv) != 0) {
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
