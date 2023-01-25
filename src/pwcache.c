// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include "pwcache.h"
#include "darray.h"
#include "trie.h"
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/** Represents cache hits for negative results. */
static void *MISSING = &MISSING;

/** Callback type for bfs_getent(). */
typedef void *bfs_getent_fn(const void *key, void *ent, void *buf, size_t bufsize);

/** Shared scaffolding for get{pw,gr}{nam,?id}_r(). */
static void *bfs_getent(struct trie_leaf *leaf, bfs_getent_fn *fn, const void *key, size_t entsize, size_t bufsize) {
	if (leaf->value) {
		errno = 0;
		return leaf->value == MISSING ? NULL : leaf->value;
	}

	void *buf = NULL;
	while (true) {
		void *result = buf;
		buf = realloc(buf, entsize + bufsize);
		if (!buf) {
			free(result);
			return NULL;
		}

		result = fn(key, buf, (char *)buf + entsize, bufsize);
		if (result) {
			leaf->value = result;
			return result;
		} else if (errno == 0) {
			free(buf);
			leaf->value = MISSING;
			return NULL;
		} else if (errno == ERANGE) {
			bufsize *= 2;
		} else {
			free(buf);
			return NULL;
		}
	}
}

/** Flush a single cache. */
static void bfs_pwcache_flush(struct trie *trie) {
	TRIE_FOR_EACH(trie, leaf) {
		if (leaf->value != MISSING) {
			free(leaf->value);
		}
		trie_remove(trie, leaf);
	}
}

struct bfs_users {
	/** Initial buffer size for getpw*_r(). */
	size_t bufsize;
	/** A map from usernames to entries. */
	struct trie by_name;
	/** A map from UIDs to entries. */
	struct trie by_uid;
};

struct bfs_users *bfs_users_new(void) {
	struct bfs_users *users = malloc(sizeof(*users));
	if (!users) {
		return NULL;
	}

	long bufsize = sysconf(_SC_GETPW_R_SIZE_MAX);
	if (bufsize > 0) {
		users->bufsize = bufsize;
	} else {
		users->bufsize = 1024;
	}

	trie_init(&users->by_name);
	trie_init(&users->by_uid);
	return users;
}

/** bfs_getent() callback for getpwnam_r(). */
static void *bfs_getpwnam_impl(const void *key, void *ent, void *buf, size_t bufsize) {
	struct passwd *result;
	errno = getpwnam_r(key, ent, buf, bufsize, &result);
	return result;
}

const struct passwd *bfs_getpwnam(struct bfs_users *users, const char *name) {
	struct trie_leaf *leaf = trie_insert_str(&users->by_name, name);
	if (!leaf) {
		return NULL;
	}

	return bfs_getent(leaf, bfs_getpwnam_impl, name, sizeof(struct passwd), users->bufsize);
}

/** bfs_getent() callback for getpwuid_r(). */
static void *bfs_getpwuid_impl(const void *key, void *ent, void *buf, size_t bufsize) {
	struct passwd *result;
	errno = getpwuid_r(*(const uid_t *)key, ent, buf, bufsize, &result);
	return result;
}

const struct passwd *bfs_getpwuid(struct bfs_users *users, uid_t uid) {
	struct trie_leaf *leaf = trie_insert_mem(&users->by_uid, &uid, sizeof(uid));
	if (!leaf) {
		return NULL;
	}

	return bfs_getent(leaf, bfs_getpwuid_impl, &uid, sizeof(struct passwd), users->bufsize);
}

void bfs_users_flush(struct bfs_users *users) {
	bfs_pwcache_flush(&users->by_name);
	bfs_pwcache_flush(&users->by_uid);
}

void bfs_users_free(struct bfs_users *users) {
	if (users) {
		bfs_users_flush(users);
		trie_destroy(&users->by_uid);
		trie_destroy(&users->by_name);
		free(users);
	}
}

struct bfs_groups {
	/** Initial buffer size for getgr*_r(). */
	size_t bufsize;
	/** A map from group names to entries. */
	struct trie by_name;
	/** A map from GIDs to entries. */
	struct trie by_gid;
};

struct bfs_groups *bfs_groups_new(void) {
	struct bfs_groups *groups = malloc(sizeof(*groups));
	if (!groups) {
		return NULL;
	}

	long bufsize = sysconf(_SC_GETGR_R_SIZE_MAX);
	if (bufsize > 0) {
		groups->bufsize = bufsize;
	} else {
		groups->bufsize = 1024;
	}

	trie_init(&groups->by_name);
	trie_init(&groups->by_gid);
	return groups;
}

/** bfs_getent() callback for getgrnam_r(). */
static void *bfs_getgrnam_impl(const void *key, void *ent, void *buf, size_t bufsize) {
	struct group *result;
	errno = getgrnam_r(key, ent, buf, bufsize, &result);
	return result;
}

const struct group *bfs_getgrnam(struct bfs_groups *groups, const char *name) {
	struct trie_leaf *leaf = trie_insert_str(&groups->by_name, name);
	if (!leaf) {
		return NULL;
	}

	return bfs_getent(leaf, bfs_getgrnam_impl, name, sizeof(struct group), groups->bufsize);
}

/** bfs_getent() callback for getgrgid_r(). */
static void *bfs_getgrgid_impl(const void *key, void *ent, void *buf, size_t bufsize) {
	struct group *result;
	errno = getgrgid_r(*(const gid_t *)key, ent, buf, bufsize, &result);
	return result;
}

const struct group *bfs_getgrgid(struct bfs_groups *groups, gid_t gid) {
	struct trie_leaf *leaf = trie_insert_mem(&groups->by_gid, &gid, sizeof(gid));
	if (!leaf) {
		return NULL;
	}

	return bfs_getent(leaf, bfs_getgrgid_impl, &gid, sizeof(struct group), groups->bufsize);
}

void bfs_groups_flush(struct bfs_groups *groups) {
	bfs_pwcache_flush(&groups->by_name);
	bfs_pwcache_flush(&groups->by_gid);
}

void bfs_groups_free(struct bfs_groups *groups) {
	if (groups) {
		bfs_groups_flush(groups);
		trie_destroy(&groups->by_gid);
		trie_destroy(&groups->by_name);
		free(groups);
	}
}
