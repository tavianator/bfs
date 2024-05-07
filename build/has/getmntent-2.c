// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include <stdio.h>
#include <sys/mnttab.h>

int main(void) {
	struct mnttab mnt;
	return getmntent(stdin, &mnt);
}
