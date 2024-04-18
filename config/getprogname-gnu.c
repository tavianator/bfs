// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include <errno.h>

int main(void) {
	const char *str = program_invocation_short_name;
	return str[0];
}
