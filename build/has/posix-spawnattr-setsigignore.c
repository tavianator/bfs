// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

// https://www.qnx.com/developers/docs/8.0/com.qnx.doc.neutrino.lib_ref/topic/p/posix_spawnattr_setsigignore.html

#include <signal.h>
#include <spawn.h>

int main(void) {
	posix_spawnattr_t attr;
	posix_spawnattr_init(&attr);
	sigset_t signals;
	sigemptyset(&signals);
	posix_spawnattr_setsigignore(&attr, &signals);
	return 0;
}
