// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include <stddef.h>
#include <sys/types.h>
#include <sys/extattr.h>

int main(void) {
	return extattr_list_link("link", EXTATTR_NAMESPACE_USER, NULL, 0);
}
