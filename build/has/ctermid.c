// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include <stdio.h>

int main(void) {
	char path[L_ctermid];
	return !ctermid(path);
}
