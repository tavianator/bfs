/****************************************************************************
 * bfs                                                                      *
 * Copyright (C) 2015-2019 Tavian Barnes <tavianator@tavianator.com>        *
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
 * - main(): the entry point for bfs(1), a breadth-first version of find(1)
 *     - main.c        (this file)
 *
 * - parse_cmdline(): parses the command line into an expression tree
 *     - cmdline.h     (declares the parsed command line structure)
 *     - expr.h        (declares the expression tree nodes)
 *     - parse.c       (the parser itself)
 *     - opt.c         (the expression optimizer)
 *
 * - eval_cmdline(): runs the expression on every file it sees
 *     - eval.[ch]     (the main evaluation functions)
 *     - exec.[ch]     (implements -exec[dir]/-ok[dir])
 *     - printf.[ch]   (implements -[f]printf)
 *
 * - bftw(): used by eval_cmdline() to walk the directory tree(s)
 *     - bftw.[ch]     (an extended version of nftw(3))
 *
 * - Utilities:
 *     - bfs.h         (constants about bfs itself)
 *     - color.[ch]    (for pretty terminal colors)
 *     - darray.[ch]   (a dynamic array library)
 *     - diag.[ch]     (formats diagnostic messages)
 *     - dstring.[ch]  (a dynamic string library)
 *     - fsade.[ch]    (a facade over non-standard filesystem features)
 *     - mtab.[ch]     (parses the system's mount table)
 *     - passwd.[ch]   (a cache for the user/group tables)
 *     - spawn.[ch]    (spawns processes)
 *     - stat.[ch]     (wraps stat(), or statx() on Linux)
 *     - time.[ch]     (date/time handling utilities)
 *     - trie.[ch]     (a trie set/map implementation)
 *     - typo.[ch]     (fuzzy matching for typos)
 *     - util.[ch]     (everything else)
 */

#include "cmdline.h"
#include "util.h"
#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

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
	int ret = EXIT_FAILURE;

	// Make sure the standard streams are open
	if (open_std_streams() != 0) {
		goto done;
	}

	// Use the system locale instead of "C"
	setlocale(LC_ALL, "");

	struct cmdline *cmdline = parse_cmdline(argc, argv);
	if (cmdline) {
		ret = eval_cmdline(cmdline);
	}

	if (free_cmdline(cmdline) != 0 && ret == EXIT_SUCCESS) {
		ret = EXIT_FAILURE;
	}

done:
	return ret;
}
