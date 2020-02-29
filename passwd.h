/****************************************************************************
 * bfs                                                                      *
 * Copyright (C) 2020 Tavian Barnes <tavianator@tavianator.com>             *
 *                                                                          *
 * Permission to use, copy, modify, and/or distribute this software for any *
 * purpose with or without fee is hereby granted.                           *
 *                                                                          *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES *
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF         *
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR  *
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES   *
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN    *
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF  *
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.           *
 ****************************************************************************/

/**
 * A caching wrapper for /etc/{passwd,group}.
 */

#ifndef BFS_PASSWD_H
#define BFS_PASSWD_H

#include <grp.h>
#include <pwd.h>

/**
 * The user table.
 */
struct bfs_users;

/**
 * Parse the user table.
 *
 * @return
 *         The parsed user table, or NULL on failure.
 */
struct bfs_users *bfs_parse_users(void);

/**
 * Get a user entry by name.
 *
 * @param users
 *         The user table.
 * @param name
 *         The username to look up.
 * @return
 *         The matching user, or NULL if not found.
 */
const struct passwd *bfs_getpwnam(const struct bfs_users *users, const char *name);

/**
 * Get a user entry by ID.
 *
 * @param users
 *         The user table.
 * @param uid
 *         The ID to look up.
 * @return
 *         The matching user, or NULL if not found.
 */
const struct passwd *bfs_getpwuid(const struct bfs_users *users, uid_t uid);

/**
 * Free a user table.
 *
 * @param users
 *         The user table to free.
 */
void bfs_free_users(struct bfs_users *users);

/**
 * The group table.
 */
struct bfs_groups;

/**
 * Parse the group table.
 *
 * @return
 *         The parsed group table, or NULL on failure.
 */
struct bfs_groups *bfs_parse_groups(void);

/**
 * Get a group entry by name.
 *
 * @param groups
 *         The group table.
 * @param name
 *         The group name to look up.
 * @return
 *         The matching group, or NULL if not found.
 */
const struct group *bfs_getgrnam(const struct bfs_groups *groups, const char *name);

/**
 * Get a group entry by ID.
 *
 * @param groups
 *         The group table.
 * @param uid
 *         The ID to look up.
 * @return
 *         The matching group, or NULL if not found.
 */
const struct group *bfs_getgrgid(const struct bfs_groups *groups, gid_t gid);

/**
 * Free a group table.
 *
 * @param groups
 *         The group table to free.
 */
void bfs_free_groups(struct bfs_groups *groups);

#endif // BFS_PASSWD_H
