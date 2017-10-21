/****************************************************************************
 * bfs                                                                      *
 * Copyright (C) 2015-2017 Tavian Barnes <tavianator@tavianator.com>        *
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

#include "cmdline.h"
#include "util.h"
#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/**
 * Ensure that a file descriptor is open.
 */
static int ensure_fd_open(int fd, int flags) {
	if (isopen(fd)) {
		return 0;
	} else if (redirect(fd, "/dev/null", flags) >= 0) {
		return 0;
	} else {
		return -1;
	}
}

/**
 * bfs entry point.
 */
int main(int argc, char *argv[]) {
	int ret = EXIT_FAILURE;

	if (ensure_fd_open(STDIN_FILENO, O_RDONLY) != 0) {
		goto done;
	}
	if (ensure_fd_open(STDOUT_FILENO, O_WRONLY) != 0) {
		goto done;
	}
	if (ensure_fd_open(STDERR_FILENO, O_WRONLY) != 0) {
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
