// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include <sys/sysctl.h>

int main(void) {
	return sysctlbyname("", NULL, 0, NULL, 0);
}
