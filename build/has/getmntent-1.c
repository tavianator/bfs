// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include <mntent.h>
#include <stdio.h>

int main(void) {
	return !getmntent(stdin);
}
