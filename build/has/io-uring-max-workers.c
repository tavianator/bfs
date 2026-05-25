// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include <liburing.h>

int main(void) {
	struct io_uring ring;
	io_uring_queue_init(1, &ring, 0);
	unsigned int values[] = {0, 0};
	return io_uring_register_iowq_max_workers(&ring, values);
}
