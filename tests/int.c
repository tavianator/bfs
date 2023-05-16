// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#undef NDEBUG

#include "../src/int.h"
#include "../src/diag.h"
#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>

bfs_static_assert(UMAX_WIDTH(0x1) == 1);
bfs_static_assert(UMAX_WIDTH(0x3) == 2);
bfs_static_assert(UMAX_WIDTH(0x7) == 3);
bfs_static_assert(UMAX_WIDTH(0xF) == 4);
bfs_static_assert(UMAX_WIDTH(0xFF) == 8);
bfs_static_assert(UMAX_WIDTH(0xFFF) == 12);
bfs_static_assert(UMAX_WIDTH(0xFFFF) == 16);

#define UWIDTH_MAX(n) (2 * ((UINTMAX_C(1) << ((n) - 1)) - 1) + 1)
#define IWIDTH_MAX(n) UWIDTH_MAX((n) - 1)
#define IWIDTH_MIN(n) (-(intmax_t)IWIDTH_MAX(n) - 1)

bfs_static_assert(UCHAR_MAX == UWIDTH_MAX(UCHAR_WIDTH));
bfs_static_assert(SCHAR_MIN == IWIDTH_MIN(SCHAR_WIDTH));
bfs_static_assert(SCHAR_MAX == IWIDTH_MAX(SCHAR_WIDTH));

bfs_static_assert(USHRT_MAX == UWIDTH_MAX(USHRT_WIDTH));
bfs_static_assert(SHRT_MIN == IWIDTH_MIN(SHRT_WIDTH));
bfs_static_assert(SHRT_MAX == IWIDTH_MAX(SHRT_WIDTH));

bfs_static_assert(UINT_MAX == UWIDTH_MAX(UINT_WIDTH));
bfs_static_assert(INT_MIN == IWIDTH_MIN(INT_WIDTH));
bfs_static_assert(INT_MAX == IWIDTH_MAX(INT_WIDTH));

bfs_static_assert(ULONG_MAX == UWIDTH_MAX(ULONG_WIDTH));
bfs_static_assert(LONG_MIN == IWIDTH_MIN(LONG_WIDTH));
bfs_static_assert(LONG_MAX == IWIDTH_MAX(LONG_WIDTH));

bfs_static_assert(ULLONG_MAX == UWIDTH_MAX(ULLONG_WIDTH));
bfs_static_assert(LLONG_MIN == IWIDTH_MIN(LLONG_WIDTH));
bfs_static_assert(LLONG_MAX == IWIDTH_MAX(LLONG_WIDTH));

bfs_static_assert(SIZE_MAX == UWIDTH_MAX(SIZE_WIDTH));
bfs_static_assert(PTRDIFF_MIN == IWIDTH_MIN(PTRDIFF_WIDTH));
bfs_static_assert(PTRDIFF_MAX == IWIDTH_MAX(PTRDIFF_WIDTH));

bfs_static_assert(UINTPTR_MAX == UWIDTH_MAX(UINTPTR_WIDTH));
bfs_static_assert(INTPTR_MIN == IWIDTH_MIN(INTPTR_WIDTH));
bfs_static_assert(INTPTR_MAX == IWIDTH_MAX(INTPTR_WIDTH));

bfs_static_assert(UINTMAX_MAX == UWIDTH_MAX(UINTMAX_WIDTH));
bfs_static_assert(INTMAX_MIN == IWIDTH_MIN(INTMAX_WIDTH));
bfs_static_assert(INTMAX_MAX == IWIDTH_MAX(INTMAX_WIDTH));

int main(void) {
	assert(bswap((uint8_t)0x12) == 0x12);
	assert(bswap((uint16_t)0x1234) == 0x3412);
	assert(bswap((uint32_t)0x12345678) == 0x78563412);
	assert(bswap((uint64_t)0x1234567812345678) == 0x7856341278563412);

	return EXIT_SUCCESS;
}
