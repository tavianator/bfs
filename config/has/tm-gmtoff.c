// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include <time.h>

int main(void) {
	struct tm tm = {0};
	return tm.tm_gmtoff;
}
