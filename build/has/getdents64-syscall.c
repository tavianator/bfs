// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include <dirent.h>
#include <sys/syscall.h>
#include <unistd.h>

int main(void) {
	char buf[1024];
	return syscall(SYS_getdents64, 3, (void *)buf, sizeof(buf));
}
