// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include <sys/types.h>
#include <sys/acl.h>

int main(void) {
	acl_t acl = acl_get_fd(3);
	int trivial;
	acl_is_trivial_np(acl, &trivial);
	return 0;
}
