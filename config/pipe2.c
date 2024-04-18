// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include <fcntl.h>
#include <unistd.h>

int main(void) {
	int fds[2];
	return pipe2(fds, O_CLOEXEC);
}
