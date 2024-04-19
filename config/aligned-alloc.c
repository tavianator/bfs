// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include <stdlib.h>

int main(void) {
	return !aligned_alloc(_Alignof(void *), sizeof(void *));
}
