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

#ifndef BFS_PWCACHE_H
#define BFS_PWCACHE_H

#include <grp.h>
#include <pwd.h>

/**
 * A user cache.
 */
struct bfs_users;

/**
 * Create a user cache.
 *
 * @return
 *         A new user cache, or NULL on failure.
 */
struct bfs_users *bfs_users_new(void);

/**
 * Get a user entry by name.
 *
 * @param users
 *         The user cache.
 * @param name
 *         The username to look up.
 * @return
 *         The matching user, or NULL if not found (errno == 0) or an error
 *         occurred (errno != 0).
 */
const struct passwd *bfs_getpwnam(struct bfs_users *users, const char *name);

/**
 * Get a user entry by ID.
 *
 * @param users
 *         The user cache.
 * @param uid
 *         The ID to look up.
 * @return
 *         The matching user, or NULL if not found (errno == 0) or an error
 *         occurred (errno != 0).
 */
const struct passwd *bfs_getpwuid(struct bfs_users *users, uid_t uid);

/**
 * Free a user cache.
 *
 * @param users
 *         The user cache to free.
 */
void bfs_users_free(struct bfs_users *users);

/**
 * A group cache.
 */
struct bfs_groups;

/**
 * Create a group cache.
 *
 * @return
 *         A new group cache, or NULL on failure.
 */
struct bfs_groups *bfs_groups_new(void);

/**
 * Get a group entry by name.
 *
 * @param groups
 *         The group cache.
 * @param name
 *         The group name to look up.
 * @return
 *         The matching group, or NULL if not found (errno == 0) or an error
 *         occurred (errno != 0).
 */
const struct group *bfs_getgrnam(struct bfs_groups *groups, const char *name);

/**
 * Get a group entry by ID.
 *
 * @param groups
 *         The group cache.
 * @param uid
 *         The ID to look up.
 * @return
 *         The matching group, or NULL if not found (errno == 0) or an error
 *         occurred (errno != 0).
 */
const struct group *bfs_getgrgid(struct bfs_groups *groups, gid_t gid);

/**
 * Free a group cache.
 *
 * @param groups
 *         The group cache to free.
 */
void bfs_groups_free(struct bfs_groups *groups);

#endif // BFS_PWCACHE_H
