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

struct bfs_mtab {
	/** A map from device ID to file system type. */
	struct trie types;
	/** The names of all the mount points. */
	struct trie names;
};

/**
 * Add an entry to the mount table.
 */
static int bfs_mtab_add(struct bfs_mtab *mtab, const char *path, dev_t dev, const char *type) {
	if (!trie_insert_str(&mtab->names, xbasename(path))) {
		return -1;
	}

	struct trie_leaf *leaf = trie_insert_mem(&mtab->types, &dev, sizeof(dev));
	if (!leaf) {
		return -1;
	}

	if (leaf->value) {
		return 0;
	}

	leaf->value = strdup(type);
	if (leaf->value) {
		return 0;
	} else {
		trie_remove(&mtab->types, leaf);
		return -1;
	}
}

struct bfs_mtab *parse_bfs_mtab() {
#if BFS_MNTENT

	FILE *file = setmntent(_PATH_MOUNTED, "r");
	if (!file) {
		// In case we're in a chroot or something with /proc but no /etc/mtab
		file = setmntent("/proc/mounts", "r");
	}
	if (!file) {
		goto fail;
	}

	struct bfs_mtab *mtab = malloc(sizeof(*mtab));
	if (!mtab) {
		goto fail_file;
	}
	trie_init(&mtab->types);
	trie_init(&mtab->names);

	struct mntent *mnt;
	while ((mnt = getmntent(file))) {
		struct bfs_stat sb;
		if (bfs_stat(AT_FDCWD, mnt->mnt_dir, BFS_STAT_NOFOLLOW, &sb) != 0) {
			continue;
		}

		if (bfs_mtab_add(mtab, mnt->mnt_dir, sb.dev, mnt->mnt_type) != 0) {
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
	trie_init(&mtab->types);
	trie_init(&mtab->names);

	for (struct statfs *mnt = mntbuf; mnt < mntbuf + size; ++mnt) {
		struct bfs_stat sb;
		if (bfs_stat(AT_FDCWD, mnt->f_mntonname, BFS_STAT_NOFOLLOW, &sb) != 0) {
			continue;
		}

		if (bfs_mtab_add(mtab, mnt->f_mntonname, sb.dev, mnt->f_fstypename) != 0) {
			goto fail_mtab;
		}
	}

	return mtab;

fail_mtab:
	free_bfs_mtab(mtab);
fail:
	return NULL;

#elif BFS_MNTTAB

	FILE *file = fopen(MNTTAB, "r");
	if (!file) {
		goto fail;
	}

	struct bfs_mtab *mtab = malloc(sizeof(*mtab));
	if (!mtab) {
		goto fail_file;
	}
	trie_init(&mtab->types);
	trie_init(&mtab->names);

	struct mnttab mnt;
	while (getmntent(file, &mnt) == 0) {
		struct bfs_stat sb;
		if (bfs_stat(AT_FDCWD, mnt.mnt_mountp, BFS_STAT_NOFOLLOW, &sb) != 0) {
			continue;
		}

		if (bfs_mtab_add(mtab, mnt.mnt_mountp, sb.dev, mnt.mnt_fstype) != 0) {
			goto fail_mtab;
		}
	}

	fclose(file);
	return mtab;

fail_mtab:
	free_bfs_mtab(mtab);
fail_file:
	fclose(file);
fail:
	return NULL;

#else

	errno = ENOTSUP;
	return NULL;
#endif
}

const char *bfs_fstype(const struct bfs_mtab *mtab, const struct bfs_stat *statbuf) {
	const struct trie_leaf *leaf = trie_find_mem(&mtab->types, &statbuf->dev, sizeof(statbuf->dev));
	if (leaf) {
		return leaf->value;
	} else {
		return "unknown";
	}
}

bool bfs_maybe_mount(const struct bfs_mtab *mtab, const char *path) {
	const char *name = xbasename(path);
	return trie_find_str(&mtab->names, name);
}

void free_bfs_mtab(struct bfs_mtab *mtab) {
	if (mtab) {
		trie_destroy(&mtab->names);

		struct trie_leaf *leaf;
		while ((leaf = trie_first_leaf(&mtab->types))) {
			free(leaf->value);
			trie_remove(&mtab->types, leaf);
		}
		trie_destroy(&mtab->types);

		free(mtab);
	}
}
