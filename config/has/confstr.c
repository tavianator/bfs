// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include <unistd.h>

int main(void) {
	confstr(_CS_PATH, NULL, 0);
	return 0;
}
