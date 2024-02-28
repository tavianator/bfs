// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

/**
 * Unit tests.
 */

#ifndef BFS_TESTS_H
#define BFS_TESTS_H

#include "../src/config.h"

/** Unit test function type. */
typedef bool test_fn(void);

/** Memory allocation tests. */
bool check_alloc(void);

/** Standard library wrapper tests. */
bool check_bfstd(void);

/** Bit manipulation tests. */
bool check_bit(void);

/** I/O queue tests. */
bool check_ioq(void);

/** Trie tests. */
bool check_trie(void);

/** Time tests. */
bool check_xtime(void);

#endif // BFS_TESTS_H
