// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include <time.h>

int main(void) {
	timer_t timer;
	return timer_create(CLOCK_REALTIME, NULL, &timer);
}
