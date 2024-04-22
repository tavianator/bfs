// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include <sys/acl.h>

int main(void) {
	return acl_trivial(".");
}
