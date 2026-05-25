// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

// https://github.com/tavianator/bfs/issues/215
#if __ANDROID__ && __ANDROID_API__ < 30
#  error "seccomp will kill you for using statx on Android < 11"
#endif

#include <fcntl.h>
#include <linux/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

int main(void) {
	struct statx sb;
	return syscall(SYS_statx, AT_FDCWD, ".", 0, STATX_BASIC_STATS, &sb);
}
