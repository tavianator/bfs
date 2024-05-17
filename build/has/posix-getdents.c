// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include <dirent.h>

int main(void) {
	char buf[1024];
	return posix_getdents(3, (void *)buf, sizeof(buf), 0);
}
