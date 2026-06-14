// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include <dirent.h>

int main(void) {
#if __APPLE__ && __has_builtin(__builtin_available)
	if (__builtin_available(macos 26.4, ios 26.4, watchos 26.4, tvos 26.4, visionos 26.4, *))
#endif
	return fdclosedir(opendir("."));
}
