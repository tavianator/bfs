/*********************************************************************
 * bfs                                                               *
 * Copyright (C) 2015 Tavian Barnes <tavianator@tavianator.com>      *
 *                                                                   *
 * This program is free software. It comes without any warranty, to  *
 * the extent permitted by applicable law. You can redistribute it   *
 * and/or modify it under the terms of the Do What The Fuck You Want *
 * To Public License, Version 2, as published by Sam Hocevar. See    *
 * the COPYING file or http://www.wtfpl.net/ for more details.       *
 *********************************************************************/

#include "bfs.h"
#include "util.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/**
 * Ensure that a file descriptor is open.
 */
static int ensure_fd_open(int fd, int flags) {
	if (isopen(fd)) {
		return 0;
	} else {
		return redirect(fd, "/dev/null", flags);
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

	struct cmdline *cmdline = parse_cmdline(argc, argv);
	if (cmdline) {
		if (eval_cmdline(cmdline) == 0) {
			ret = EXIT_SUCCESS;
		}
	}

	free_cmdline(cmdline);

done:
	return ret;
}
