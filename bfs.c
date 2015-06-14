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
#include <stdio.h>
#include <stdlib.h>

static int callback(const char *fpath, int typeflag, void *ptr) {
	printf("%s\n", fpath);
	return BFTW_CONTINUE;
}

int main(int argc, char* argv[]) {
	const char* path = ".";
	if (argc > 1) {
		path = argv[1];
	}

	// TODO: getrlimit(RLIMIT_NOFILE)
	if (bftw(path, callback, 1024, NULL) != 0) {
		perror("bftw()");
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
