// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

/**
 * - main(): the entry point for bfs(1), a breadth-first version of find(1)
 *     - main.c        (this file)
 *
 * - bfs_parse_cmdline(): parses the command line into an expression tree
 *     - ctx.[ch]      (struct bfs_ctx, the overall bfs context)
 *     - expr.h        (declares the expression tree nodes)
 *     - parse.[ch]    (the parser itself)
 *     - opt.[ch]      (the optimizer)
 *
 * - bfs_eval(): runs the expression on every file it sees
 *     - eval.[ch]     (the main evaluation functions)
 *     - exec.[ch]     (implements -exec[dir]/-ok[dir])
 *     - printf.[ch]   (implements -[f]printf)
 *
 * - bftw(): used by bfs_eval() to walk the directory tree(s)
 *     - bftw.[ch]     (an extended version of nftw(3))
 *
 * - Utilities:
 *     - prelude.h     (feature test macros; automatically included)
 *     - alloc.[ch]    (memory allocation)
 *     - atomic.h      (atomic operations)
 *     - bar.[ch]      (a terminal status bar)
 *     - bit.h         (bit manipulation)
 *     - bfs.h         (configuration and fundamental utilities)
 *     - bfstd.[ch]    (standard library wrappers/polyfills)
 *     - color.[ch]    (for pretty terminal colors)
 *     - diag.[ch]     (formats diagnostic messages)
 *     - dir.[ch]      (a directory API facade)
 *     - dstring.[ch]  (a dynamic string library)
 *     - fsade.[ch]    (a facade over non-standard filesystem features)
 *     - ioq.[ch]      (an async I/O queue)
 *     - list.h        (linked list macros)
 *     - mtab.[ch]     (parses the system's mount table)
 *     - pwcache.[ch]  (a cache for the user/group tables)
 *     - sanity.h      (sanitizer interfaces)
 *     - sighook.[ch]  (signal hooks)
 *     - stat.[ch]     (wraps stat(), or statx() on Linux)
 *     - thread.h      (multi-threading)
 *     - trie.[ch]     (a trie set/map implementation)
 *     - typo.[ch]     (fuzzy matching for typos)
 *     - version.c     (embeds version information)
 *     - xregex.[ch]   (regular expression support)
 *     - xspawn.[ch]   (spawns processes)
 *     - xtime.[ch]    (date/time handling utilities)
 */

#include "bfstd.h"
#include "ctx.h"
#include "diag.h"
#include "eval.h"
#include "parse.h"

#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

/**
 * Check if a file descriptor is open.
 */
static bool isopen(int fd) {
	return fcntl(fd, F_GETFD) >= 0 || errno != EBADF;
}

/**
 * Open a file and redirect it to a particular descriptor.
 */
static int redirect(int fd, const char *path, int flags) {
	int newfd = open(path, flags);
	if (newfd < 0 || newfd == fd) {
		return newfd;
	}

	int ret = dup2(newfd, fd);
	close_quietly(newfd);
	return ret;
}

/**
 * Make sure the standard streams std{in,out,err} are open.  If they are not,
 * future open() calls may use those file descriptors, and std{in,out,err} will
 * use them unintentionally.
 */
static int open_std_streams(void) {
#ifdef O_PATH
	const int inflags = O_PATH, outflags = O_PATH;
#else
	// These are intentionally backwards so that bfs >&- still fails with EBADF
	const int inflags = O_WRONLY, outflags = O_RDONLY;
#endif

	if (!isopen(STDERR_FILENO) && redirect(STDERR_FILENO, "/dev/null", outflags) < 0) {
		return -1;
	}
	if (!isopen(STDOUT_FILENO) && redirect(STDOUT_FILENO, "/dev/null", outflags) < 0) {
		perror("redirect()");
		return -1;
	}
	if (!isopen(STDIN_FILENO) && redirect(STDIN_FILENO, "/dev/null", inflags) < 0) {
		perror("redirect()");
		return -1;
	}

	return 0;
}

/**
 * bfs entry point.
 */
int main(int argc, char *argv[]) {
	// Make sure the standard streams are open
	if (open_std_streams() != 0) {
		return EXIT_FAILURE;
	}

	// Use the system locale instead of "C"
	int locale_err = 0;
	if (!setlocale(LC_ALL, "")) {
		locale_err = errno;
	}

	// Apply the environment's timezone
	tzset();

	// Parse the command line
	struct bfs_ctx *ctx = bfs_parse_cmdline(argc, argv);
	if (!ctx) {
		return EXIT_FAILURE;
	}

	// Warn if setlocale() failed, unless there's no expression to evaluate
	if (locale_err && ctx->warn && ctx->expr) {
		bfs_warning(ctx, "Failed to set locale: %s\n\n", xstrerror(locale_err));
	}

	// Walk the file system tree, evaluating the expression on each file
	int ret = bfs_eval(ctx);

	// Free the parsed command line, and detect any last-minute errors
	if (bfs_ctx_free(ctx) != 0 && ret == EXIT_SUCCESS) {
		ret = EXIT_FAILURE;
	}

	return ret;
}
