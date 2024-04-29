// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include <dirent.h>

int main(void) {
	struct dirent de;
	getdents(3, &de, 1024);
	return 0;
}
