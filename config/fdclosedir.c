// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include <dirent.h>

int main(void) {
	return fdclosedir(opendir("."));
}
