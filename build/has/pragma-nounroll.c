// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

/// -Werror

int main(void) {
#pragma nounroll
	for (int i = 0; i < 100; ++i);
	return 0;
}
