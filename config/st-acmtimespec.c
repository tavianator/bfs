// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include <sys/stat.h>

int main(void) {
	struct stat sb = {0};
	unsigned int a = sb.st_atimespec.tv_sec;
	unsigned int c = sb.st_ctimespec.tv_sec;
	unsigned int m = sb.st_mtimespec.tv_sec;
	return a + c + m;
}
