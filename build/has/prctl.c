// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include <sys/prctl.h>

int main(void) {
	return prctl(PR_GET_DUMPABLE);
}
