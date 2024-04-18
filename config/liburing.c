// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include <liburing.h>

int main(void) {
	io_uring_free_probe(0);
	return 0;
}
