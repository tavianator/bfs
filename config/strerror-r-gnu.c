// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include <errno.h>
#include <string.h>

int main(void) {
	char buf[256];
	// Check that strerror_r() returns a pointer
	return *strerror_r(ENOMEM, buf, sizeof(buf));
}
