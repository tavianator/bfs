// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include <sys/stat.h>

int main(void) {
	struct stat sb = {0};
	return sb.st_birthtimespec.tv_sec;
}
