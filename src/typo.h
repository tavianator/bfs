// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#ifndef BFS_TYPO_H
#define BFS_TYPO_H

/**
 * Find the "typo" distance between two strings.
 *
 * @actual
 *         The actual string typed by the user.
 * @expected
 *         The expected valid string.
 * @return The distance between the two strings.
 */
int typo_distance(const char *actual, const char *expected);

#endif // BFS_TYPO_H
