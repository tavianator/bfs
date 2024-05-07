// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

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
 * Flush a user cache.
 *
 * @param users
 *         The cache to flush.
 */
void bfs_users_flush(struct bfs_users *users);

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
 * Flush a group cache.
 *
 * @param groups
 *         The cache to flush.
 */
void bfs_groups_flush(struct bfs_groups *groups);

/**
 * Free a group cache.
 *
 * @param groups
 *         The group cache to free.
 */
void bfs_groups_free(struct bfs_groups *groups);

#endif // BFS_PWCACHE_H
