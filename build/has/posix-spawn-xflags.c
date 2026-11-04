// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

// https://www.qnx.com/developers/docs/8.0/com.qnx.doc.neutrino.lib_ref/topic/p/posix_spawnattr_setxflags.html

#include <spawn.h>
#include <stdint.h>

int main(void) {
	posix_spawnattr_t attr;
	posix_spawnattr_init(&attr);
	uint32_t xflags;
	posix_spawnattr_getxflags(&attr, &xflags);
	posix_spawnattr_setxflags(&attr, xflags);
	return 0;
}
