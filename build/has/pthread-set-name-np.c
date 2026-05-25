// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include <pthread.h>
#include <pthread_np.h>

int main(void) {
	pthread_set_name_np(pthread_self(), "name");
	return 0;
}
