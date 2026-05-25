// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include <stddef.h>
#include <sys/types.h>
#include <sys/acl.h>

int main(void) {
	acl_t acl = acl_get_file(".", ACL_TYPE_DEFAULT);
	return acl == (acl_t)NULL;
}
