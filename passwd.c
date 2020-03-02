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

#include "passwd.h"
#include "darray.h"
#include "trie.h"
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>

struct bfs_users {
	/** The array of passwd entries. */
	struct passwd *entries;
	/** A map from usernames to entries. */
	struct trie by_name;
	/** A map from UIDs to entries. */
	struct trie by_uid;
};

struct bfs_users *bfs_parse_users(void) {
	int error;

	struct bfs_users *users = malloc(sizeof(*users));
	if (!users) {
		return NULL;
	}

	users->entries = NULL;
	trie_init(&users->by_name);
	trie_init(&users->by_uid);

	setpwent();

	while (true) {
		errno = 0;
		struct passwd *ent = getpwent();
		if (!ent) {
			if (errno) {
				error = errno;
				goto fail_end;
			} else {
				break;
			}
		}

		if (DARRAY_PUSH(&users->entries, ent) != 0) {
			error = errno;
			goto fail_end;
		}

		ent = users->entries + darray_length(users->entries) - 1;
		ent->pw_name = strdup(ent->pw_name);
		ent->pw_dir = strdup(ent->pw_dir);
		ent->pw_shell = strdup(ent->pw_shell);
		if (!ent->pw_name || !ent->pw_dir || !ent->pw_shell) {
			error = ENOMEM;
			goto fail_end;
		}
	}

	endpwent();

	for (size_t i = 0; i < darray_length(users->entries); ++i) {
		struct passwd *entry = users->entries + i;
		struct trie_leaf *leaf = trie_insert_str(&users->by_name, entry->pw_name);
		if (leaf) {
			if (!leaf->value) {
				leaf->value = entry;
			}
		} else {
			error = errno;
			goto fail_free;
		}

		leaf = trie_insert_mem(&users->by_uid, &entry->pw_uid, sizeof(entry->pw_uid));
		if (leaf) {
			if (!leaf->value) {
				leaf->value = entry;
			}
		} else {
			error = errno;
			goto fail_free;
		}
	}

	return users;

fail_end:
	endpwent();
fail_free:
	bfs_free_users(users);
	errno = error;
	return NULL;
}

const struct passwd *bfs_getpwnam(const struct bfs_users *users, const char *name) {
	const struct trie_leaf *leaf = trie_find_str(&users->by_name, name);
	if (leaf) {
		return leaf->value;
	} else {
		return NULL;
	}
}

const struct passwd *bfs_getpwuid(const struct bfs_users *users, uid_t uid) {
	const struct trie_leaf *leaf = trie_find_mem(&users->by_uid, &uid, sizeof(uid));
	if (leaf) {
		return leaf->value;
	} else {
		return NULL;
	}
}

void bfs_free_users(struct bfs_users *users) {
	if (users) {
		trie_destroy(&users->by_uid);
		trie_destroy(&users->by_name);

		for (size_t i = 0; i < darray_length(users->entries); ++i) {
			struct passwd *entry = users->entries + i;
			free(entry->pw_shell);
			free(entry->pw_dir);
			free(entry->pw_name);
		}
		darray_free(users->entries);

		free(users);
	}
}

struct bfs_groups {
	/** The array of group entries. */
	struct group *entries;
	/** A map from group names to entries. */
	struct trie by_name;
	/** A map from GIDs to entries. */
	struct trie by_gid;
};

struct bfs_groups *bfs_parse_groups(void) {
	int error;

	struct bfs_groups *groups = malloc(sizeof(*groups));
	if (!groups) {
		return NULL;
	}

	groups->entries = NULL;
	trie_init(&groups->by_name);
	trie_init(&groups->by_gid);

	setgrent();

	while (true) {
		errno = 0;
		struct group *ent = getgrent();
		if (!ent) {
			if (errno) {
				error = errno;
				goto fail_end;
			} else {
				break;
			}
		}

		if (DARRAY_PUSH(&groups->entries, ent) != 0) {
			error = errno;
			goto fail_end;
		}

		ent = groups->entries + darray_length(groups->entries) - 1;
		ent->gr_name = strdup(ent->gr_name);
		if (!ent->gr_name) {
			error = errno;
			goto fail_end;
		}

		char **members = ent->gr_mem;
		ent->gr_mem = NULL;
		for (char **mem = members; *mem; ++mem) {
			char *dup = strdup(*mem);
			if (!dup) {
				error = errno;
				goto fail_end;
			}

			if (DARRAY_PUSH(&ent->gr_mem, &dup) != 0) {
				error = errno;
				free(dup);
				goto fail_end;
			}
		}
	}

	endgrent();

	for (size_t i = 0; i < darray_length(groups->entries); ++i) {
		struct group *entry = groups->entries + i;
		struct trie_leaf *leaf = trie_insert_str(&groups->by_name, entry->gr_name);
		if (leaf) {
			if (!leaf->value) {
				leaf->value = entry;
			}
		} else {
			error = errno;
			goto fail_free;
		}

		leaf = trie_insert_mem(&groups->by_gid, &entry->gr_gid, sizeof(entry->gr_gid));
		if (leaf) {
			if (!leaf->value) {
				leaf->value = entry;
			}
		} else {
			error = errno;
			goto fail_free;
		}
	}

	return groups;

fail_end:
	endgrent();
fail_free:
	bfs_free_groups(groups);
	errno = error;
	return NULL;
}

const struct group *bfs_getgrnam(const struct bfs_groups *groups, const char *name) {
	const struct trie_leaf *leaf = trie_find_str(&groups->by_name, name);
	if (leaf) {
		return leaf->value;
	} else {
		return NULL;
	}
}

const struct group *bfs_getgrgid(const struct bfs_groups *groups, gid_t gid) {
	const struct trie_leaf *leaf = trie_find_mem(&groups->by_gid, &gid, sizeof(gid));
	if (leaf) {
		return leaf->value;
	} else {
		return NULL;
	}
}

void bfs_free_groups(struct bfs_groups *groups) {
	if (groups) {
		trie_destroy(&groups->by_gid);
		trie_destroy(&groups->by_name);

		for (size_t i = 0; i < darray_length(groups->entries); ++i) {
			struct group *entry = groups->entries + i;
			for (size_t j = 0; j < darray_length(entry->gr_mem); ++j) {
				free(entry->gr_mem[j]);
			}
			darray_free(entry->gr_mem);
			free(entry->gr_name);
		}
		darray_free(groups->entries);

		free(groups);
	}
}
