// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include <stdlib.h>

int main(void) {
	const char *str = getprogname();
	return str[0];
}
