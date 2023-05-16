// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

/**
 * Bits & bytes.
 */

#ifndef BFS_INT_H
#define BFS_INT_H

#include <limits.h>
#include <stdint.h>

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
#ifndef UCHAR_WIDTH
#  define UCHAR_WIDTH CHAR_WIDTH
#endif
#ifndef SCHAR_WIDTH
#  define SCHAR_WIDTH CHAR_WIDTH
#endif
#ifndef USHRT_WIDTH
#  define USHRT_WIDTH UMAX_WIDTH(USHRT_MAX)
#endif
#ifndef SHRT_WIDTH
#  define SHRT_WIDTH USHRT_WIDTH
#endif
#ifndef UINT_WIDTH
#  define UINT_WIDTH UMAX_WIDTH(UINT_MAX)
#endif
#ifndef INT_WIDTH
#  define INT_WIDTH UINT_WIDTH
#endif
#ifndef ULONG_WIDTH
#  define ULONG_WIDTH UMAX_WIDTH(ULONG_MAX)
#endif
#ifndef LONG_WIDTH
#  define LONG_WIDTH ULONG_WIDTH
#endif
#ifndef ULLONG_WIDTH
#  define ULLONG_WIDTH UMAX_WIDTH(ULLONG_MAX)
#endif
#ifndef LLONG_WIDTH
#  define LLONG_WIDTH ULLONG_WIDTH
#endif
#ifndef SIZE_WIDTH
#  define SIZE_WIDTH UMAX_WIDTH(SIZE_MAX)
#endif
#ifndef PTRDIFF_WIDTH
#  define PTRDIFF_WIDTH (UMAX_WIDTH(PTRDIFF_MAX) + 1)
#endif
#ifndef UINTPTR_WIDTH
#  define UINTPTR_WIDTH UMAX_WIDTH(UINTPTR_MAX)
#endif
#ifndef INTPTR_WIDTH
#  define INTPTR_WIDTH UINTPTR_WIDTH
#endif
#ifndef UINTMAX_WIDTH
#  define UINTMAX_WIDTH UMAX_WIDTH(UINTMAX_MAX)
#endif
#ifndef INTMAX_WIDTH
#  define INTMAX_WIDTH UINTMAX_WIDTH
#endif

#endif // BFS_INT_H
