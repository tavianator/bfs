// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include <sched.h>

int main(void) {
	cpu_set_t set;
	return sched_getaffinity(0, sizeof(set), &set);
}
