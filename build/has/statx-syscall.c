// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include <fcntl.h>
#include <linux/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

int main(void) {
	struct statx sb;
	syscall(SYS_statx, AT_FDCWD, ".", 0, STATX_BASIC_STATS, &sb);
	return 0;
}
