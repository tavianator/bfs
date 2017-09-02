/****************************************************************************
 * bfs                                                                      *
 * Copyright (C) 2017 Tavian Barnes <tavianator@tavianator.com>             *
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
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifndef __has_include
#	define __has_include(header) 0
#endif

#if __GLIBC__ || __has_include(<mntent.h>)
#	define BFS_MNTENT 1
#elif BSD
#	define BFS_MNTINFO 1
#endif

#if BFS_MNTENT
#	include <mntent.h>
#	include <paths.h>
#elif BFS_MNTINFO
#	include <sys/mount.h>
#	include <sys/ucred.h>
#endif

/**
 * A mount point in the mount table.
 */
struct bfs_mtab_entry {
	/** The device number for this mount point. */
	dev_t dev;
	/** The file system type of this mount point. */
	char *type;
};

struct bfs_mtab {
	/** The array of mtab entries. */
	struct bfs_mtab_entry *table;
	/** The size of the array. */
	size_t size;
	/** Capacity of the array. */
	size_t capacity;
};

/**
 * Add an entry to the mount table.
 */
static int bfs_mtab_push(struct bfs_mtab *mtab, dev_t dev, const char *type) {
	size_t size = mtab->size + 1;

	if (size >= mtab->capacity) {
		size_t capacity = 2*size;
		struct bfs_mtab_entry *table = realloc(mtab->table, capacity*sizeof(*table));
		if (!table) {
			return -1;
		}
		mtab->table = table;
		mtab->capacity = capacity;
	}

	struct bfs_mtab_entry *entry = mtab->table + (size - 1);
	entry->dev = dev;
	entry->type = strdup(type);
	if (!entry->type) {
		return -1;
	}

	mtab->size = size;
	return 0;
}

struct bfs_mtab *parse_bfs_mtab() {
#if BFS_MNTENT

	FILE *file = setmntent(_PATH_MOUNTED, "r");
	if (!file) {
		goto fail;
	}

	struct bfs_mtab *mtab = malloc(sizeof(*mtab));
	if (!mtab) {
		goto fail_file;
	}
	mtab->table = NULL;
	mtab->size = 0;
	mtab->capacity = 0;

	struct mntent *mnt;
	while ((mnt = getmntent(file))) {
		struct stat sb;
		if (stat(mnt->mnt_dir, &sb) != 0) {
			continue;
		}

		if (bfs_mtab_push(mtab, sb.st_dev, mnt->mnt_type) != 0) {
			goto fail_mtab;
		}
	}

	endmntent(file);
	return mtab;

fail_mtab:
	free_bfs_mtab(mtab);
fail_file:
	endmntent(file);
fail:
	return NULL;

#elif BFS_MNTINFO

	struct statfs *mntbuf;
	int size = getmntinfo(&mntbuf, MNT_WAIT);
	if (size < 0) {
		return NULL;
	}

	struct bfs_mtab *mtab = malloc(sizeof(*mtab));
	if (!mtab) {
		goto fail;
	}

	mtab->size = 0;
	mtab->table = malloc(size*sizeof(*mtab->table));
	if (!mtab->table) {
		goto fail_mtab;
	}
	mtab->capacity = size;

	for (struct statfs *mnt = mntbuf; mnt < mntbuf + size; ++mnt) {
		struct stat sb;
		if (stat(mnt->f_mntonname, &sb) != 0) {
			continue;
		}

		if (bfs_mtab_push(mtab, sb.st_dev, mnt->f_fstypename) != 0) {
			goto fail_mtab;
		}
	}

	return mtab;

fail_mtab:
	free_bfs_mtab(mtab);
fail:
	return NULL;

#else

	errno = ENOTSUP;
	return NULL;
#endif
}

const char *bfs_fstype(const struct bfs_mtab *mtab, const struct stat *statbuf) {
	for (struct bfs_mtab_entry *mnt = mtab->table; mnt < mtab->table + mtab->size; ++mnt) {
		if (statbuf->st_dev == mnt->dev) {
			return mnt->type;
		}
	}

	return "unknown";
}

void free_bfs_mtab(struct bfs_mtab *mtab) {
	if (mtab) {
		for (struct bfs_mtab_entry *mnt = mtab->table; mnt < mtab->table + mtab->size; ++mnt) {
			free(mnt->type);
		}
		free(mtab->table);
		free(mtab);
	}
}
