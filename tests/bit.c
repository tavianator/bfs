// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include "prelude.h"

#include "bfs.h"
#include "bit.h"
#include "diag.h"
#include "tests.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

// Polyfill C23's one-argument static_assert()
#if __STDC_VERSION__ < C23
#  undef static_assert
#  define static_assert(...) _Static_assert(__VA_ARGS__, #__VA_ARGS__)
#endif

static_assert(UMAX_WIDTH(0x1) == 1);
static_assert(UMAX_WIDTH(0x3) == 2);
static_assert(UMAX_WIDTH(0x7) == 3);
static_assert(UMAX_WIDTH(0xF) == 4);
static_assert(UMAX_WIDTH(0xFF) == 8);
static_assert(UMAX_WIDTH(0xFFF) == 12);
static_assert(UMAX_WIDTH(0xFFFF) == 16);

#define UWIDTH_MAX(n) (2 * ((UINTMAX_C(1) << ((n) - 1)) - 1) + 1)
#define IWIDTH_MAX(n) UWIDTH_MAX((n) - 1)
#define IWIDTH_MIN(n) (-(intmax_t)IWIDTH_MAX(n) - 1)

static_assert(UCHAR_MAX == UWIDTH_MAX(UCHAR_WIDTH));
static_assert(SCHAR_MIN == IWIDTH_MIN(SCHAR_WIDTH));
static_assert(SCHAR_MAX == IWIDTH_MAX(SCHAR_WIDTH));

static_assert(USHRT_MAX == UWIDTH_MAX(USHRT_WIDTH));
static_assert(SHRT_MIN == IWIDTH_MIN(SHRT_WIDTH));
static_assert(SHRT_MAX == IWIDTH_MAX(SHRT_WIDTH));

static_assert(UINT_MAX == UWIDTH_MAX(UINT_WIDTH));
static_assert(INT_MIN == IWIDTH_MIN(INT_WIDTH));
static_assert(INT_MAX == IWIDTH_MAX(INT_WIDTH));

static_assert(ULONG_MAX == UWIDTH_MAX(ULONG_WIDTH));
static_assert(LONG_MIN == IWIDTH_MIN(LONG_WIDTH));
static_assert(LONG_MAX == IWIDTH_MAX(LONG_WIDTH));

static_assert(ULLONG_MAX == UWIDTH_MAX(ULLONG_WIDTH));
static_assert(LLONG_MIN == IWIDTH_MIN(LLONG_WIDTH));
static_assert(LLONG_MAX == IWIDTH_MAX(LLONG_WIDTH));

static_assert(SIZE_MAX == UWIDTH_MAX(SIZE_WIDTH));
static_assert(PTRDIFF_MIN == IWIDTH_MIN(PTRDIFF_WIDTH));
static_assert(PTRDIFF_MAX == IWIDTH_MAX(PTRDIFF_WIDTH));

static_assert(UINTPTR_MAX == UWIDTH_MAX(UINTPTR_WIDTH));
static_assert(INTPTR_MIN == IWIDTH_MIN(INTPTR_WIDTH));
static_assert(INTPTR_MAX == IWIDTH_MAX(INTPTR_WIDTH));

static_assert(UINTMAX_MAX == UWIDTH_MAX(UINTMAX_WIDTH));
static_assert(INTMAX_MIN == IWIDTH_MIN(INTMAX_WIDTH));
static_assert(INTMAX_MAX == IWIDTH_MAX(INTMAX_WIDTH));

#define check_eq(a, b) \
	bfs_check((a) == (b), "(0x%jX) %s != %s (0x%jX)", (uintmax_t)(a), #a, #b, (uintmax_t)(b))

void check_bit(void) {
	const char *str = "\x1\x2\x3\x4";
	uint32_t word;
	memcpy(&word, str, sizeof(word));

#if ENDIAN_NATIVE == ENDIAN_LITTLE
	check_eq(word, 0x04030201);
#elif ENDIAN_NATIVE == ENDIAN_BIG
	check_eq(word, 0x01020304);
#else
#  warning "Skipping byte order tests on mixed/unknown-endian machine"
#endif

	check_eq(bswap((uint8_t)0x12), 0x12);
	check_eq(bswap((uint16_t)0x1234), 0x3412);
	check_eq(bswap((uint32_t)0x12345678), 0x78563412);
	check_eq(bswap((uint64_t)0x1234567812345678), 0x7856341278563412);

	check_eq(count_ones(0x0U), 0);
	check_eq(count_ones(0x1U), 1);
	check_eq(count_ones(0x2U), 1);
	check_eq(count_ones(0x3U), 2);
	check_eq(count_ones(0x137FU), 10);

	check_eq(count_zeros(0U), UINT_WIDTH);
	check_eq(count_zeros(0UL), ULONG_WIDTH);
	check_eq(count_zeros(0ULL), ULLONG_WIDTH);
	check_eq(count_zeros((uint8_t)0), 8);
	check_eq(count_zeros((uint16_t)0), 16);
	check_eq(count_zeros((uint32_t)0), 32);
	check_eq(count_zeros((uint64_t)0), 64);

	check_eq(rotate_left((uint8_t)0xA1, 4), 0x1A);
	check_eq(rotate_left((uint16_t)0x1234, 12), 0x4123);
	check_eq(rotate_left((uint32_t)0x12345678, 20), 0x67812345);
	check_eq(rotate_left((uint32_t)0x12345678, 0), 0x12345678);

	check_eq(rotate_right((uint8_t)0xA1, 4), 0x1A);
	check_eq(rotate_right((uint16_t)0x1234, 12), 0x2341);
	check_eq(rotate_right((uint32_t)0x12345678, 20), 0x45678123);
	check_eq(rotate_right((uint32_t)0x12345678, 0), 0x12345678);

	for (unsigned int i = 0; i < 16; ++i) {
		uint16_t n = (uint16_t)1 << i;
		for (unsigned int j = i; j < 16; ++j) {
			uint16_t m = (uint16_t)1 << j;
			uint16_t nm = n | m;
			check_eq(count_ones(nm), 1 + (n != m));
			check_eq(count_zeros(nm), 15 - (n != m));
			check_eq(leading_zeros(nm), 15 - j);
			check_eq(trailing_zeros(nm), i);
			check_eq(first_leading_one(nm), 16 - j);
			check_eq(first_trailing_one(nm), i + 1);
			check_eq(bit_width(nm), j + 1);
			check_eq(bit_floor(nm), m);
			if (n == m) {
				check_eq(bit_ceil(nm), m);
				bfs_check(has_single_bit(nm));
			} else {
				if (j < 15) {
					check_eq(bit_ceil(nm), (m << 1));
				}
				bfs_check(!has_single_bit(nm));
			}
		}
	}

	check_eq(leading_zeros((uint16_t)0), 16);
	check_eq(trailing_zeros((uint16_t)0), 16);
	check_eq(first_leading_one(0U), 0);
	check_eq(first_trailing_one(0U), 0);
	check_eq(bit_width(0U), 0);
	check_eq(bit_floor(0U), 0);
	check_eq(bit_ceil(0U), 1);

	bfs_check(!has_single_bit(0U));
	bfs_check(!has_single_bit(UINT32_MAX));
	bfs_check(has_single_bit((uint32_t)1 << (UINT_WIDTH - 1)));
}
