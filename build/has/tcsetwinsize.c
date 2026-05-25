// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include <termios.h>

int main(void) {
	const struct winsize ws = {0};
	return tcsetwinsize(0, &ws);
}
