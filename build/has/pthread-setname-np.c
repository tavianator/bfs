// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include <pthread.h>

int main(void) {
	return pthread_setname_np(pthread_self(), "name");
}
