// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include <stdio.h>

int main(void) {
	return dprintf(1, "%s\n", "Hello world!");
}
