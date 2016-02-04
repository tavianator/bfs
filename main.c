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
#include <stdlib.h>

int main(int argc, char *argv[]) {
	int ret = EXIT_FAILURE;

	struct cmdline *cl = parse_cmdline(argc, argv);
	if (cl) {
		if (eval_cmdline(cl) == 0) {
			ret = EXIT_SUCCESS;
		}
	}

	free_cmdline(cl);
	return ret;
}
