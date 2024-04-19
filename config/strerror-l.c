// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include <errno.h>
#include <locale.h>
#include <string.h>

int main(void) {
	locale_t locale = duplocale(LC_GLOBAL_LOCALE);
	return !strerror_l(ENOMEM, locale);
}
