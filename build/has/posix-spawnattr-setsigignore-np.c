// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

// https://docs.oracle.com/cd/E36784_01/html/E36874/posix-spawnattr-setsigignore-np-3c.html

#include <signal.h>
#include <spawn.h>

int main(void) {
	posix_spawnattr_t attr;
	posix_spawnattr_init(&attr);
	sigset_t signals;
	sigemptyset(&signals);
	posix_spawnattr_setsigignore_np(&attr, &signals);
	return 0;
}
