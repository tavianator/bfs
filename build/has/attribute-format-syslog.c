// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include <stdio.h>

__attribute__((format(syslog, 1, 2)))
static int foo(const char *format, ...) {
	return 0;
}

int main(void) {
	return foo("%s: %m\n", "main()");
}
