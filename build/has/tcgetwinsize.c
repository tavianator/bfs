// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include <termios.h>

int main(void) {
	struct winsize ws;
	return tcgetwinsize(0, &ws);
}
