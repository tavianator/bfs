// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include <locale.h>

int main(void) {
	locale_t locale = uselocale((locale_t)0);
	return locale == LC_GLOBAL_LOCALE;
}
