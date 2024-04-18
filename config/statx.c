// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include <fcntl.h>
#include <sys/stat.h>

int main(void) {
	struct statx sb;
	statx(AT_FDCWD, ".", 0, STATX_BASIC_STATS, &sb);
	return 0;
}
