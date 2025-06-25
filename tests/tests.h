// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

/**
 * Unit tests.
 */

#ifndef BFS_TESTS_H
#define BFS_TESTS_H

#include "bfs.h"
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
#define bfs_check(cond, ...) \
	bfs_check_impl((cond) || (bfs_check_(#cond, __VA_ARGS__), false))

#define bfs_check_(str, ...) \
	BFS_VA_IF(__VA_ARGS__) \
		(bfs_diag(__VA_ARGS__)) \
		(bfs_diag("Check failed: `%s`", str))

/**
 * Check a condition, logging the current error string on failure.
 */
#define bfs_echeck(cond, ...) \
	bfs_check_impl((cond) || (bfs_echeck_(#cond, __VA_ARGS__), false))

#define bfs_echeck_(str, ...) \
	BFS_VA_IF(__VA_ARGS__) \
		(bfs_ediag(__VA_ARGS__)) \
		(bfs_ediag("Check failed: `%s`", str))

#endif // BFS_TESTS_H
