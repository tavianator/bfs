// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

/**
 * Unit tests.
 */

#ifndef BFS_TESTS_H
#define BFS_TESTS_H

#include "../src/config.h"
#include "../src/diag.h"

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

/** Process spawning tests. */
bool check_xspawn(void);

/** Time tests. */
bool check_xtime(void);

/** Don't ignore the bfs_check() return value. */
attr(nodiscard)
static inline bool bfs_check(bool ret) {
	return ret;
}

/**
 * Check a condition, logging a message on failure but continuing.
 */
#define bfs_check(...) \
	bfs_check(bfs_check_(#__VA_ARGS__, __VA_ARGS__, "", ""))

#define bfs_check_(str, cond, format, ...) \
	((cond) ? true : (bfs_diag( \
		sizeof(format) > 1 \
			? "%.0s" format "%s%s" \
			: "Check failed: `%s`%s", \
		str, __VA_ARGS__), false))

/** Get a string description of the last error. */
const char *bfs_errstr(void);

/**
 * Check a condition, logging the current error string on failure.
 */
#define bfs_pcheck(...) \
	bfs_pcheck_(#__VA_ARGS__, __VA_ARGS__, "", "")

#define bfs_pcheck_(str, cond, format, ...) \
	((cond) ? true : (bfs_diag( \
		sizeof(format) > 1 \
			? "%.0s" format "%s%s: %s" \
			: "Check failed: `%s`%s: %s", \
		str, __VA_ARGS__, bfs_errstr()), false))

#endif // BFS_TESTS_H
