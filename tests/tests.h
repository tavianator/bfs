// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

/**
 * Unit tests.
 */

#ifndef BFS_TESTS_H
#define BFS_TESTS_H

#include "prelude.h"
#include "bfstd.h"
#include "diag.h"

/** Memory allocation tests. */
void check_alloc(void);

/** Standard library wrapper tests. */
void check_bfstd(void);

/** Bit manipulation tests. */
void check_bit(void);

/** I/O queue tests. */
void check_ioq(void);

/** Linked list tests. */
void check_list(void);

/** Signal hook tests. */
void check_sighook(void);

/** Trie tests. */
void check_trie(void);

/** Process spawning tests. */
void check_xspawn(void);

/** Time tests. */
void check_xtime(void);

/** Record a single check and return the result. */
bool bfs_check_impl(bool result);

/**
 * Check a condition, logging a message on failure but continuing.
 */
#define bfs_check(...) \
	bfs_check_impl(bfs_check_(#__VA_ARGS__, __VA_ARGS__, "", ""))

#define bfs_check_(str, cond, format, ...) \
	((cond) ? true : (bfs_diag( \
		sizeof(format) > 1 \
			? "%.0s" format "%s%s" \
			: "Check failed: `%s`%s", \
		str, __VA_ARGS__), false))

/**
 * Check a condition, logging the current error string on failure.
 */
#define bfs_echeck(...) \
	bfs_check_impl(bfs_echeck_(#__VA_ARGS__, __VA_ARGS__, "", errstr()))

#define bfs_echeck_(str, cond, format, ...) \
	((cond) ? true : (bfs_diag( \
		sizeof(format) > 1 \
			? "%.0s" format "%s: %s" \
			: "Check failed: `%s`: %s", \
		str, __VA_ARGS__), false))

#endif // BFS_TESTS_H
