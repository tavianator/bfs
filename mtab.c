/****************************************************************************
 * bfs                                                                      *
 * Copyright (C) 2017-2019 Tavian Barnes <tavianator@tavianator.com>        *
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

#include "mtab.h"
#include "darray.h"
#include "trie.h"
#include "util.h"
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#if BFS_HAS_SYS_PARAM
#	include <sys/param.h>
#endif

#if BFS_HAS_MNTENT
#	define BFS_MNTENT 1
#elif BSD
#	define BFS_MNTINFO 1
#elif __SVR4
#	define BFS_MNTTAB 1
#endif

#if BFS_MNTENT
#	include <mntent.h>
#	include <paths.h>
#	include <stdio.h>
#elif BFS_MNTINFO
#	include <sys/mount.h>
#	include <sys/ucred.h>
#elif BFS_MNTTAB
#	include <stdio.h>
#	include <sys/mnttab.h>
#endif

/**
 * A mount point in the table.
 */
struct bfs_mtab_entry {
	/** The path to the mount point. */
	char *path;
	/** The filesystem type. */
	char *type;
};

struct bfs_mtab {
	/** The list of mount points. */
	struct bfs_mtab_entry *entries;
	/** The basenames of every mount point. */
	struct trie names;

	/** A map from device ID to fstype (populated lazily). */
	struct trie types;
	/** Whether the types map has been populated. */
	bool types_filled;
};

/**
 * Add an entry to the mount table.
 */
static int bfs_mtab_add(struct bfs_mtab *mtab, const char *path, const char *type) {
	struct bfs_mtab_entry entry = {
		.path = strdup(path),
		.type = strdup(type),
	};

	if (!entry.path || !entry.type) {
		goto fail_entry;
	}

	if (DARRAY_PUSH(&mtab->entries, &entry) != 0) {
		goto fail_entry;
	}

	if (!trie_insert_str(&mtab->names, xbasename(path))) {
		goto fail;
	}

	return 0;

fail_entry:
	free(entry.type);
	free(entry.path);
fail:
	return -1;
}

struct bfs_mtab *parse_bfs_mtab() {
	struct bfs_mtab *mtab = malloc(sizeof(*mtab));
	if (!mtab) {
		return NULL;
	}

	mtab->entries = NULL;
	trie_init(&mtab->names);
	trie_init(&mtab->types);
	mtab->types_filled = false;

	int error = 0;

#if BFS_MNTENT

	FILE *file = setmntent(_PATH_MOUNTED, "r");
	if (!file) {
		// In case we're in a chroot or something with /proc but no /etc/mtab
		error = errno;
		file = setmntent("/proc/mounts", "r");
	}
	if (!file) {
		goto fail;
	}

	struct mntent *mnt;
	while ((mnt = getmntent(file))) {
		if (bfs_mtab_add(mtab, mnt->mnt_dir, mnt->mnt_type) != 0) {
			error = errno;
			endmntent(file);
			goto fail;
		}
	}

	endmntent(file);

#elif BFS_MNTINFO

#if __NetBSD__
	typedef struct statvfs bfs_statfs;
#else
	typedef struct statfs bfs_statfs;
#endif

	bfs_statfs *mntbuf;
	int size = getmntinfo(&mntbuf, MNT_WAIT);
	if (size < 0) {
		error = errno;
		goto fail;
	}

	for (bfs_statfs *mnt = mntbuf; mnt < mntbuf + size; ++mnt) {
		if (bfs_mtab_add(mtab, mnt->f_mntonname, mnt->f_fstypename) != 0) {
			error = errno;
			goto fail;
		}
	}

#elif BFS_MNTTAB

	FILE *file = fopen(MNTTAB, "r");
	if (!file) {
		error = errno;
		goto fail;
	}

	struct mnttab mnt;
	while (getmntent(file, &mnt) == 0) {
		if (bfs_mtab_add(mtab, mnt.mnt_mountp, mnt.mnt_fstype) != 0) {
			error = errno;
			fclose(file);
			goto fail;
		}
	}

	fclose(file);

#else

	error = ENOTSUP;
	goto fail;

#endif

	return mtab;

fail:
	free_bfs_mtab(mtab);
	errno = error;
	return NULL;
}

static void bfs_mtab_fill_types(struct bfs_mtab *mtab) {
	for (size_t i = 0; i < darray_length(mtab->entries); ++i) {
		struct bfs_mtab_entry *entry = mtab->entries + i;

		struct bfs_stat sb;
		if (bfs_stat(AT_FDCWD, entry->path, BFS_STAT_NOFOLLOW | BFS_STAT_NOSYNC, &sb) != 0) {
			continue;
		}

		struct trie_leaf *leaf = trie_insert_mem(&mtab->types, &sb.dev, sizeof(sb.dev));
		if (leaf) {
			leaf->value = entry->type;
		}
	}

	mtab->types_filled = true;
}

const char *bfs_fstype(const struct bfs_mtab *mtab, const struct bfs_stat *statbuf) {
	if (!mtab->types_filled) {
		bfs_mtab_fill_types((struct bfs_mtab *)mtab);
	}

	const struct trie_leaf *leaf = trie_find_mem(&mtab->types, &statbuf->dev, sizeof(statbuf->dev));
	if (leaf) {
		return leaf->value;
	} else {
		return "unknown";
	}
}

bool bfs_might_be_mount(const struct bfs_mtab *mtab, const char *path) {
	const char *name = xbasename(path);
	return trie_find_str(&mtab->names, name);
}

void free_bfs_mtab(struct bfs_mtab *mtab) {
	if (mtab) {
		trie_destroy(&mtab->types);
		trie_destroy(&mtab->names);

		for (size_t i = 0; i < darray_length(mtab->entries); ++i) {
			free(mtab->entries[i].type);
			free(mtab->entries[i].path);
		}
		darray_free(mtab->entries);

		free(mtab);
	}
}
