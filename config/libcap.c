// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include <sys/capability.h>

int main(void) {
	cap_free(0);
	return 0;
}
