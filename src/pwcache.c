// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include "pwcache.h"
#include "alloc.h"
#include "config.h"
#include "darray.h"
#include "trie.h"
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/** Represents cache hits for negative results. */
static void *MISSING = &MISSING;

/** Callback type for bfs_getent(). */
typedef void *bfs_getent_fn(const void *key, void *ptr, size_t bufsize);

/** Shared scaffolding for get{pw,gr}{nam,?id}_r(). */
static void *bfs_getent(bfs_getent_fn *fn, const void *key, struct trie_leaf *leaf, struct varena *varena, size_t bufsize) {
	if (leaf->value) {
		errno = 0;
		return leaf->value == MISSING ? NULL : leaf->value;
	}

	void *ptr = varena_alloc(varena, bufsize);
	if (!ptr) {
		return NULL;
	}

	while (true) {
		void *ret = fn(key, ptr, bufsize);
		if (ret) {
			leaf->value = ret;
			return ret;
		} else if (errno == 0) {
			leaf->value = MISSING;
			break;
		} else if (errno == ERANGE) {
			void *next = varena_grow(varena, ptr, &bufsize);
			if (!next) {
				break;
			}
			ptr = next;
		} else {
			break;
		}
	}

	varena_free(varena, ptr, bufsize);
	return NULL;
}

/**
 * An arena-allocated struct passwd.
 */
struct bfs_passwd {
	struct passwd pwd;
	char buf[];
};

struct bfs_users {
	/** Initial buffer size for getpw*_r(). */
	size_t bufsize;
	/** bfs_passwd arena. */
	struct varena varena;
	/** A map from usernames to entries. */
	struct trie by_name;
	/** A map from UIDs to entries. */
	struct trie by_uid;
};

struct bfs_users *bfs_users_new(void) {
	struct bfs_users *users = ALLOC(struct bfs_users);
	if (!users) {
		return NULL;
	}

	long bufsize = sysconf(_SC_GETPW_R_SIZE_MAX);
	if (bufsize > 0) {
		users->bufsize = bufsize;
	} else {
		users->bufsize = 1024;
	}

	VARENA_INIT(&users->varena, struct bfs_passwd, buf);
	trie_init(&users->by_name);
	trie_init(&users->by_uid);
	return users;
}

/** bfs_getent() callback for getpwnam_r(). */
static void *bfs_getpwnam_impl(const void *key, void *ptr, size_t bufsize) {
	struct bfs_passwd *storage = ptr;

	struct passwd *ret;
	errno = getpwnam_r(key, &storage->pwd, storage->buf, bufsize, &ret);
	return ret;
}

const struct passwd *bfs_getpwnam(struct bfs_users *users, const char *name) {
	struct trie_leaf *leaf = trie_insert_str(&users->by_name, name);
	if (!leaf) {
		return NULL;
	}

	return bfs_getent(bfs_getpwnam_impl, name, leaf, &users->varena, users->bufsize);
}

/** bfs_getent() callback for getpwuid_r(). */
static void *bfs_getpwuid_impl(const void *key, void *ptr, size_t bufsize) {
	const uid_t *uid = key;
	struct bfs_passwd *storage = ptr;

	struct passwd *ret;
	errno = getpwuid_r(*uid, &storage->pwd, storage->buf, bufsize, &ret);
	return ret;
}

const struct passwd *bfs_getpwuid(struct bfs_users *users, uid_t uid) {
	struct trie_leaf *leaf = trie_insert_mem(&users->by_uid, &uid, sizeof(uid));
	if (!leaf) {
		return NULL;
	}

	return bfs_getent(bfs_getpwuid_impl, &uid, leaf, &users->varena, users->bufsize);
}

void bfs_users_flush(struct bfs_users *users) {
	trie_clear(&users->by_uid);
	trie_clear(&users->by_name);
	varena_clear(&users->varena);
}

void bfs_users_free(struct bfs_users *users) {
	if (users) {
		trie_destroy(&users->by_uid);
		trie_destroy(&users->by_name);
		varena_destroy(&users->varena);
		free(users);
	}
}

/**
 * An arena-allocated struct group.
 */
struct bfs_group {
	struct group grp;
	char buf[];
};

struct bfs_groups {
	/** Initial buffer size for getgr*_r(). */
	size_t bufsize;
	/** bfs_group arena. */
	struct varena varena;
	/** A map from group names to entries. */
	struct trie by_name;
	/** A map from GIDs to entries. */
	struct trie by_gid;
};

struct bfs_groups *bfs_groups_new(void) {
	struct bfs_groups *groups = ALLOC(struct bfs_groups);
	if (!groups) {
		return NULL;
	}

	long bufsize = sysconf(_SC_GETGR_R_SIZE_MAX);
	if (bufsize > 0) {
		groups->bufsize = bufsize;
	} else {
		groups->bufsize = 1024;
	}

	VARENA_INIT(&groups->varena, struct bfs_group, buf);
	trie_init(&groups->by_name);
	trie_init(&groups->by_gid);
	return groups;
}

/** bfs_getent() callback for getgrnam_r(). */
static void *bfs_getgrnam_impl(const void *key, void *ptr, size_t bufsize) {
	struct bfs_group *storage = ptr;

	struct group *ret;
	errno = getgrnam_r(key, &storage->grp, storage->buf, bufsize, &ret);
	return ret;
}

const struct group *bfs_getgrnam(struct bfs_groups *groups, const char *name) {
	struct trie_leaf *leaf = trie_insert_str(&groups->by_name, name);
	if (!leaf) {
		return NULL;
	}

	return bfs_getent(bfs_getgrnam_impl, name, leaf, &groups->varena, groups->bufsize);
}

/** bfs_getent() callback for getgrgid_r(). */
static void *bfs_getgrgid_impl(const void *key, void *ptr, size_t bufsize) {
	const gid_t *gid = key;
	struct bfs_group *storage = ptr;

	struct group *ret;
	errno = getgrgid_r(*gid, &storage->grp, storage->buf, bufsize, &ret);
	return ret;
}

const struct group *bfs_getgrgid(struct bfs_groups *groups, gid_t gid) {
	struct trie_leaf *leaf = trie_insert_mem(&groups->by_gid, &gid, sizeof(gid));
	if (!leaf) {
		return NULL;
	}

	return bfs_getent(bfs_getgrgid_impl, &gid, leaf, &groups->varena, groups->bufsize);
}

void bfs_groups_flush(struct bfs_groups *groups) {
	trie_clear(&groups->by_gid);
	trie_clear(&groups->by_name);
	varena_clear(&groups->varena);
}

void bfs_groups_free(struct bfs_groups *groups) {
	if (groups) {
		trie_destroy(&groups->by_gid);
		trie_destroy(&groups->by_name);
		varena_destroy(&groups->varena);
		free(groups);
	}
}
