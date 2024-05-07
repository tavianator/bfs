// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include <stddef.h>
#include <sys/types.h>
#include <sys/mount.h>

int main(void) {
	return getmntinfo(NULL, MNT_WAIT);
}
