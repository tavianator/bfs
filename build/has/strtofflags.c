// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include <stddef.h>
#include <unistd.h>

int main(void) {
	return strtofflags(NULL, NULL, NULL);
}
