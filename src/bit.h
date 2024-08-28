// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

/**
 * Bits & bytes.
 */

#ifndef BFS_BIT_H
#define BFS_BIT_H

#include "bfs.h"

#include <limits.h>
#include <stdint.h>

#if __has_include(<stdbit.h>)
#  include <stdbit.h>
#endif

// C23 polyfill: _WIDTH macros

// The U*_MAX macros are of the form 2**n - 1, and we want to extract the n.
// One way would be *_WIDTH = popcount(*_MAX).  Alternatively, we can use
// Hallvard B. Furuseth's technique from [1], which is shorter.
//
// [1]: https://groups.google.com/g/comp.lang.c/c/NfedEFBFJ0k

// Let mask be of the form 2**m - 1, e.g. 0b111, and let n range over
// [0b0, 0b1, 0b11, 0b111, 0b1111, ...].  Then we have
//
//     n % 0b111
//         == [0b0, 0b1, 0b11, 0b0, 0b1, 0b11, ...]
//     n / (n % 0b111 + 1)
//         == [0b0 (x3), 0b111 (x3), 0b111111 (x3), ...]
//     n / (n % 0b111 + 1) / 0b111
//         == [0b0 (x3), 0b1 (x3), 0b1001 (x3), 0b1001001 (x3), ...]
//     n / (n % 0b111 + 1) / 0b111 % 0b111
//         == [0 (x3), 1 (x3), 2 (x3), ...]
//         == UMAX_CHUNK(n, 0b111)
#define UMAX_CHUNK(n, mask) (n / (n % mask + 1) / mask % mask)

// 8 * UMAX_CHUNK(n, 255) gives [0 (x8), 8 (x8), 16 (x8), ...].  To that we add
// [0, 1, 2, ..., 6, 7, 0, 1, ...], which we get from a linear interpolation on
// n % 255:
//
//     n % 255
//         == [0, 1, 3, 7, 15, 31, 63, 127, 0, ...]
//     86 / (n % 255 + 12)
//         == [7, 6, 5, 4, 3, 2, 1, 0, 7, ...]
#define UMAX_INTERP(n) (7 - 86 / (n % 255 + 12))

#define UMAX_WIDTH(n) (8 * UMAX_CHUNK(n, 255) + UMAX_INTERP(n))

#ifndef CHAR_WIDTH
#  define CHAR_WIDTH CHAR_BIT
#endif

// See https://gcc.gnu.org/onlinedocs/cpp/Common-Predefined-Macros.html

#ifndef USHRT_WIDTH
#  ifdef __SHRT_WIDTH__
#    define USHRT_WIDTH __SHRT_WIDTH__
#  else
#    define USHRT_WIDTH UMAX_WIDTH(USHRT_MAX)
#  endif
#endif

#ifndef UINT_WIDTH
#  ifdef __INT_WIDTH__
#    define UINT_WIDTH __INT_WIDTH__
#  else
#    define UINT_WIDTH UMAX_WIDTH(UINT_MAX)
#  endif
#endif

#ifndef ULONG_WIDTH
#  ifdef __LONG_WIDTH__
#    define ULONG_WIDTH __LONG_WIDTH__
#  else
#    define ULONG_WIDTH UMAX_WIDTH(ULONG_MAX)
#  endif
#endif

#ifndef ULLONG_WIDTH
#  ifdef __LONG_LONG_WIDTH__
#    define ULLONG_WIDTH __LONG_LONG_WIDTH__
#  elif defined(__LLONG_WIDTH__) // Clang
#    define ULLONG_WIDTH __LLONG_WIDTH__
#  else
#    define ULLONG_WIDTH UMAX_WIDTH(ULLONG_MAX)
#  endif
#endif

#ifndef SIZE_WIDTH
#  ifdef __SIZE_WIDTH__
#    define SIZE_WIDTH __SIZE_WIDTH__
#  else
#    define SIZE_WIDTH UMAX_WIDTH(SIZE_MAX)
#  endif
#endif

#ifndef PTRDIFF_WIDTH
#  ifdef __PTRDIFF_WIDTH__
#    define PTRDIFF_WIDTH __PTRDIFF_WIDTH__
#  else
#    define PTRDIFF_WIDTH UMAX_WIDTH(PTRDIFF_MAX)
#  endif
#endif

#ifndef UINTPTR_WIDTH
#  ifdef __INTPTR_WIDTH__
#    define UINTPTR_WIDTH __INTPTR_WIDTH__
#  else
#    define UINTPTR_WIDTH UMAX_WIDTH(UINTPTR_MAX)
#  endif
#endif

#ifndef UINTMAX_WIDTH
#  ifdef __INTMAX_WIDTH__
#    define UINTMAX_WIDTH __INTMAX_WIDTH__
#  else
#    define UINTMAX_WIDTH UMAX_WIDTH(UINTMAX_MAX)
#  endif
#endif

#ifndef UCHAR_WIDTH
#  define UCHAR_WIDTH CHAR_WIDTH
#endif
#ifndef SCHAR_WIDTH
#  define SCHAR_WIDTH CHAR_WIDTH
#endif
#ifndef SHRT_WIDTH
#  define SHRT_WIDTH USHRT_WIDTH
#endif
#ifndef INT_WIDTH
#  define INT_WIDTH UINT_WIDTH
#endif
#ifndef LONG_WIDTH
#  define LONG_WIDTH ULONG_WIDTH
#endif
#ifndef LLONG_WIDTH
#  define LLONG_WIDTH ULLONG_WIDTH
#endif
#ifndef INTPTR_WIDTH
#  define INTPTR_WIDTH UINTPTR_WIDTH
#endif
#ifndef INTMAX_WIDTH
#  define INTMAX_WIDTH UINTMAX_WIDTH
#endif

// C23 polyfill: byte order

#ifdef __STDC_ENDIAN_LITTLE__
#  define ENDIAN_LITTLE __STDC_ENDIAN_LITTLE__
#elif defined(__ORDER_LITTLE_ENDIAN__)
#  define ENDIAN_LITTLE __ORDER_LITTLE_ENDIAN__
#else
#  define ENDIAN_LITTLE 1234
#endif

#ifdef __STDC_ENDIAN_BIG__
#  define ENDIAN_BIG __STDC_ENDIAN_BIG__
#elif defined(__ORDER_BIG_ENDIAN__)
#  define ENDIAN_BIG __ORDER_BIG_ENDIAN__
#else
#  define ENDIAN_BIG 4321
#endif

#ifdef __STDC_ENDIAN_NATIVE__
#  define ENDIAN_NATIVE __STDC_ENDIAN_NATIVE__
#elif defined(__BYTE_ORDER__)
#  define ENDIAN_NATIVE __BYTE_ORDER__
#else
#  define ENDIAN_NATIVE 0
#endif

#if __GNUC__
#  define bswap_u16 __builtin_bswap16
#  define bswap_u32 __builtin_bswap32
#  define bswap_u64 __builtin_bswap64
#else

static inline uint16_t bswap_u16(uint16_t n) {
	return (n << 8) | (n >> 8);
}

static inline uint32_t bswap_u32(uint32_t n) {
	return ((uint32_t)bswap_u16(n) << 16) | bswap_u16(n >> 16);
}

static inline uint64_t bswap_u64(uint64_t n) {
	return ((uint64_t)bswap_u32(n) << 32) | bswap_u32(n >> 32);
}

#endif

static inline uint8_t bswap_u8(uint8_t n) {
	return n;
}

/**
 * Reverse the byte order of an integer.
 */
#define bswap(n) \
	_Generic((n), \
		uint8_t: bswap_u8, \
		uint16_t: bswap_u16, \
		uint32_t: bswap_u32, \
		uint64_t: bswap_u64)(n)

// Define an overload for each unsigned type
#define UINT_OVERLOADS(macro) \
	macro(unsigned char, _uc, UCHAR_WIDTH) \
	macro(unsigned short, _us, USHRT_WIDTH) \
	macro(unsigned int, _ui, UINT_WIDTH) \
	macro(unsigned long, _ul, ULONG_WIDTH) \
	macro(unsigned long long, _ull, ULLONG_WIDTH)

// Select an overload based on an unsigned integer type
#define UINT_SELECT(n, name) \
	_Generic((n), \
		unsigned char:      name##_uc, \
		unsigned short:     name##_us, \
		unsigned int:       name##_ui, \
		unsigned long:      name##_ul, \
		unsigned long long: name##_ull)

// C23 polyfill: bit utilities

#if __STDC_VERSION_STDBIT_H__ >= C23
#  define count_ones stdc_count_ones
#  define count_zeros stdc_count_zeros
#  define leading_zeros stdc_leading_zeros
#  define leading_ones stdc_leading_ones
#  define trailing_zeros stdc_trailing_zeros
#  define trailing_ones stdc_trailing_ones
#  define first_leading_zero stdc_first_leading_zero
#  define first_leading_one stdc_first_leading_one
#  define first_trailing_zero stdc_first_trailing_zero
#  define first_trailing_one stdc_first_trailing_one
#  define has_single_bit stdc_has_single_bit
#  define bit_width stdc_bit_width
#  define bit_ceil stdc_bit_ceil
#  define bit_floor stdc_bit_floor
#else

#if __GNUC__

// GCC provides builtins for unsigned {int,long,long long}, so promote char/short
#define UINT_BUILTIN_uc(name) __builtin_##name
#define UINT_BUILTIN_us(name) __builtin_##name
#define UINT_BUILTIN_ui(name) __builtin_##name
#define UINT_BUILTIN_ul(name) __builtin_##name##l
#define UINT_BUILTIN_ull(name) __builtin_##name##ll
#define UINT_BUILTIN(name, suffix) UINT_BUILTIN##suffix(name)

#define BUILTIN_WIDTH_uc UINT_WIDTH
#define BUILTIN_WIDTH_us UINT_WIDTH
#define BUILTIN_WIDTH_ui UINT_WIDTH
#define BUILTIN_WIDTH_ul ULONG_WIDTH
#define BUILTIN_WIDTH_ull ULLONG_WIDTH
#define BUILTIN_WIDTH(suffix) BUILTIN_WIDTH##suffix

#define COUNT_ONES(type, suffix, width) \
	static inline unsigned int count_ones##suffix(type n) { \
		return UINT_BUILTIN(popcount, suffix)(n); \
	}

#define LEADING_ZEROS(type, suffix, width) \
	static inline unsigned int leading_zeros##suffix(type n) { \
	        return n \
			? UINT_BUILTIN(clz, suffix)(n) - (BUILTIN_WIDTH(suffix) - width) \
			: width; \
	}

#define TRAILING_ZEROS(type, suffix, width) \
	static inline unsigned int trailing_zeros##suffix(type n) { \
		return n ? UINT_BUILTIN(ctz, suffix)(n) : (int)width; \
	}

#define FIRST_TRAILING_ONE(type, suffix, width) \
	static inline unsigned int first_trailing_one##suffix(type n) { \
		return UINT_BUILTIN(ffs, suffix)(n); \
	}

#else // !__GNUC__

#define COUNT_ONES(type, suffix, width) \
	static inline unsigned int count_ones##suffix(type n) { \
		int ret; \
		for (ret = 0; n; ++ret) { \
			n &= n - 1; \
		} \
		return ret; \
	}

#define LEADING_ZEROS(type, suffix, width) \
	static inline unsigned int leading_zeros##suffix(type n) { \
		type bit = (type)1 << (width - 1); \
		int ret; \
		for (ret = 0; bit && !(n & bit); ++ret, bit >>= 1); \
		return ret; \
	}

#define TRAILING_ZEROS(type, suffix, width) \
	static inline unsigned int trailing_zeros##suffix(type n) { \
		type bit = 1; \
		int ret; \
		for (ret = 0; bit && !(n & bit); ++ret, bit <<= 1); \
		return ret; \
	}

#define FIRST_TRAILING_ONE(type, suffix, width) \
	static inline unsigned int first_trailing_one##suffix(type n) { \
		return n ? trailing_zeros##suffix(n) + 1 : 0; \
	}

#endif // !__GNUC__

UINT_OVERLOADS(COUNT_ONES)
UINT_OVERLOADS(LEADING_ZEROS)
UINT_OVERLOADS(TRAILING_ZEROS)
UINT_OVERLOADS(FIRST_TRAILING_ONE)

#define FIRST_LEADING_ONE(type, suffix, width) \
	static inline unsigned int first_leading_one##suffix(type n) { \
		return n ? leading_zeros##suffix(n) + 1 : 0; \
	}

#define HAS_SINGLE_BIT(type, suffix, width) \
	static inline bool has_single_bit##suffix(type n) { \
		/** Branchless n && !(n & (n - 1)) */ \
		return n - 1 < (n ^ (n - 1)); \
	}

#define BIT_WIDTH(type, suffix, width) \
	static inline unsigned int bit_width##suffix(type n) { \
		return width - leading_zeros##suffix(n); \
	}

#define BIT_FLOOR(type, suffix, width) \
	static inline type bit_floor##suffix(type n) { \
		return n ? (type)1 << (bit_width##suffix(n) - 1) : 0; \
	}

#define BIT_CEIL(type, suffix, width) \
	static inline type bit_ceil##suffix(type n) { \
		return (type)1 << bit_width##suffix(n - !!n); \
	}

UINT_OVERLOADS(FIRST_LEADING_ONE)
UINT_OVERLOADS(HAS_SINGLE_BIT)
UINT_OVERLOADS(BIT_WIDTH)
UINT_OVERLOADS(BIT_FLOOR)
UINT_OVERLOADS(BIT_CEIL)

#define count_ones(n) UINT_SELECT(n, count_ones)(n)
#define count_zeros(n) UINT_SELECT(n, count_ones)(~(n))

#define leading_zeros(n) UINT_SELECT(n, leading_zeros)(n)
#define leading_ones(n) UINT_SELECT(n, leading_zeros)(~(n))

#define trailing_zeros(n) UINT_SELECT(n, trailing_zeros)(n)
#define trailing_ones(n) UINT_SELECT(n, trailing_zeros)(~(n))

#define first_leading_one(n) UINT_SELECT(n, first_leading_one)(n)
#define first_leading_zero(n) UINT_SELECT(n, first_leading_one)(~(n))

#define first_trailing_one(n) UINT_SELECT(n, first_trailing_one)(n)
#define first_trailing_zero(n) UINT_SELECT(n, first_trailing_one)(~(n))

#define has_single_bit(n) UINT_SELECT(n, has_single_bit)(n)

#define bit_width(n) UINT_SELECT(n, bit_width)(n)
#define bit_floor(n) UINT_SELECT(n, bit_floor)(n)
#define bit_ceil(n) UINT_SELECT(n, bit_ceil)(n)

#endif // __STDC_VERSION_STDBIT_H__ < C23

#define ROTATE_LEFT(type, suffix, width) \
	static inline type rotate_left##suffix(type n, int c) { \
		return (n << c) | (n >> ((width - c) % width)); \
	}

#define ROTATE_RIGHT(type, suffix, width) \
	static inline type rotate_right##suffix(type n, int c) { \
		return (n >> c) | (n << ((width - c) % width)); \
	}

UINT_OVERLOADS(ROTATE_LEFT)
UINT_OVERLOADS(ROTATE_RIGHT)

#define rotate_left(n, c) UINT_SELECT(n, rotate_left)(n, c)
#define rotate_right(n, c) UINT_SELECT(n, rotate_right)(n, c)

#endif // BFS_BIT_H
