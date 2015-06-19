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
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
	const char *path;
	bool hidden;
} options;

static int callback(const char *fpath, int typeflag, void *ptr) {
	const options *opts = ptr;

	const char *filename = strrchr(fpath, '/');
	if (filename) {
		++filename;
	} else {
		filename = fpath + strlen(fpath);
	}

	if (!opts->hidden && filename[0] == '.') {
		return BFTW_SKIP_SUBTREE;
	}

	printf("%s\n", fpath);
	return BFTW_CONTINUE;
}

int main(int argc, char* argv[]) {
	options opts;
	opts.path = NULL;
	opts.hidden = true;

	for (int i = 1; i < argc; ++i) {
		const char *arg = argv[i];

		if (strcmp(arg, "-hidden") == 0) {
			opts.hidden = true;
		} else if (strcmp(arg, "-nohidden") == 0) {
			opts.hidden = false;
		} else {
			if (opts.path) {
				fprintf(stderr, "Duplicate path `%s` on command line.", arg);
				return EXIT_FAILURE;
			}
			opts.path = arg;
		}
	}

	if (!opts.path) {
		opts.path = ".";
	}

	// TODO: getrlimit(RLIMIT_NOFILE)
	if (bftw(opts.path, callback, 1024, &opts) != 0) {
		perror("bftw()");
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
