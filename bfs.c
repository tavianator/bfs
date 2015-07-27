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

#include "bftw.h"
#include "color.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <unistd.h>

static int infer_nopenfd() {
	int ret = 4096;

	struct rlimit rl;
	if (getrlimit(RLIMIT_NOFILE, &rl) == 0) {
		if (rl.rlim_cur != RLIM_INFINITY) {
			ret = rl.rlim_cur;
		}
	}

	// Account for std{in,out,err}
	if (ret > 3) {
		ret -= 3;
	}

	return ret;
}

typedef struct {
	const char *path;
	color_table *colors;
	bool hidden;
} options;

static int callback(const char *fpath, const struct BFTW *ftwbuf, void *ptr) {
	const options *opts = ptr;

	if (ftwbuf->typeflag == BFTW_ERROR) {
		print_error(opts->colors, fpath, ftwbuf);
		return BFTW_SKIP_SUBTREE;
	}

	if (!opts->hidden) {
		if (ftwbuf->base > 0 && fpath[ftwbuf->base] == '.') {
			return BFTW_SKIP_SUBTREE;
		}
	}

	pretty_print(opts->colors, fpath, ftwbuf->statbuf);
	return BFTW_CONTINUE;
}

int main(int argc, char *argv[]) {
	int ret = EXIT_FAILURE;

	options opts;
	opts.path = NULL;
	opts.colors = NULL;
	opts.hidden = true;

	bool color = isatty(STDOUT_FILENO);

	for (int i = 1; i < argc; ++i) {
		const char *arg = argv[i];

		if (strcmp(arg, "-color") == 0) {
			color = true;
		} else if (strcmp(arg, "-nocolor") == 0) {
			color = false;
		} else if (strcmp(arg, "-hidden") == 0) {
			opts.hidden = true;
		} else if (strcmp(arg, "-nohidden") == 0) {
			opts.hidden = false;
		} else if (arg[0] == '-') {
			fprintf(stderr, "Unknown option `%s`.", arg);
			goto done;
		} else {
			if (opts.path) {
				fprintf(stderr, "Duplicate path `%s` on command line.", arg);
				goto done;
			}
			opts.path = arg;
		}
	}

	if (!opts.path) {
		opts.path = ".";
	}

	int flags = BFTW_RECOVER;

	if (color) {
		flags |= BFTW_STAT;
		opts.colors = parse_colors(getenv("LS_COLORS"));
	}

	int nopenfd = infer_nopenfd();
	if (bftw(opts.path, callback, nopenfd, flags, &opts) != 0) {
		perror("bftw()");
		goto done;
	}

	ret = EXIT_SUCCESS;
done:
	free_colors(opts.colors);
	return ret;
}
