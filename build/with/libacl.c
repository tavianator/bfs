// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include <sys/acl.h>

int main(void) {
	acl_free(0);
	return 0;
}
