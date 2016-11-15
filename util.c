/*********************************************************************
 * bfs                                                               *
 * Copyright (C) 2016 Tavian Barnes <tavianator@tavianator.com>      *
 *                                                                   *
 * This program is free software. It comes without any warranty, to  *
 * the extent permitted by applicable law. You can redistribute it   *
 * and/or modify it under the terms of the Do What The Fuck You Want *
 * To Public License, Version 2, as published by Sam Hocevar. See    *
 * the COPYING file or http://www.wtfpl.net/ for more details.       *
 *********************************************************************/

#include "util.h"
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

int xreaddir(DIR *dir, struct dirent **de) {
	errno = 0;
	*de = readdir(dir);
	if (!*de && errno != 0) {
		return -1;
	} else {
		return 0;
	}
}

bool isopen(int fd) {
	return fcntl(fd, F_GETFD) >= 0 || errno != EBADF;
}

int redirect(int fd, const char *path, int flags, ...) {
	close(fd);

	mode_t mode = 0;
	if (flags & O_CREAT) {
		va_list args;
		va_start(args, flags);

		// Use int rather than mode_t, because va_arg must receive a
		// fully-promoted type
		mode = va_arg(args, int);

		va_end(args);
	}

	int ret = open(path, flags, mode);

	if (ret >= 0 && ret != fd) {
		int other = ret;
		ret = dup2(other, fd);
		if (close(other) != 0) {
			ret = -1;
		}
	}

	return ret;
}
