// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include <selinux/selinux.h>

int main(void) {
	freecon(0);
	return 0;
}
