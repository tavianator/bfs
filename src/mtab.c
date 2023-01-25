// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include "mtab.h"
#include "bfstd.h"
#include "config.h"
#include "darray.h"
#include "stat.h"
#include "trie.h"
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#if BFS_USE_MNTENT_H
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

	const char *name = path + xbaseoff(path);
	if (!trie_insert_str(&mtab->names, name)) {
		goto fail;
	}

	return 0;

fail_entry:
	free(entry.type);
	free(entry.path);
fail:
	return -1;
}

struct bfs_mtab *bfs_mtab_parse(void) {
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

	FILE *file = xfopen(MNTTAB, O_RDONLY | O_CLOEXEC);
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
	bfs_mtab_free(mtab);
	errno = error;
	return NULL;
}

static int bfs_mtab_fill_types(struct bfs_mtab *mtab) {
	const enum bfs_stat_flags flags = BFS_STAT_NOFOLLOW | BFS_STAT_NOSYNC;
	int ret = -1;

	// It's possible that /path/to/mount was unmounted between bfs_mtab_parse() and bfs_mtab_fill_types().
	// In that case, the dev_t of /path/to/mount will be the same as /path/to, which should not get its
	// fstype from the old mount record of /path/to/mount.
	//
	// Detect this by comparing the st_dev of the parent (/path/to) and child (/path/to/mount).  Only when
	// they differ can the filesystem type actually change between them.  As a minor optimization, we keep
	// the parent directory open in case multiple mounts have the same parent (e.g. /mnt).
	char *parent_dir = NULL;
	int parent_fd = -1;
	struct bfs_stat parent_stat;
	int parent_ret;

	for (size_t i = 0; i < darray_length(mtab->entries); ++i) {
		struct bfs_mtab_entry *entry = &mtab->entries[i];
		const char *path = entry->path;
		int fd = AT_FDCWD;

		char *dir = xdirname(path);
		if (!dir) {
			goto fail;
		}

		if (parent_dir && strcmp(parent_dir, dir) == 0) {
			// Same parent
			free(dir);
		} else {
			free(parent_dir);
			parent_dir = dir;

			if (parent_fd >= 0) {
				xclose(parent_fd);
			}
			parent_fd = open(parent_dir, O_SEARCH | O_CLOEXEC | O_DIRECTORY);

			parent_ret = -1;
			if (parent_fd >= 0) {
				parent_ret = bfs_stat(parent_fd, NULL, flags, &parent_stat);
			}
		}

		if (parent_fd >= 0) {
			fd = parent_fd;
			path += xbaseoff(path);
		}

		struct bfs_stat sb;
		if (bfs_stat(fd, path, flags, &sb) != 0) {
			continue;
		}

		if (parent_ret == 0 && parent_stat.dev == sb.dev && parent_stat.ino != sb.ino) {
			// Not a mount point any more (or a bind mount, but with the same fstype)
			continue;
		}

		struct trie_leaf *leaf = trie_insert_mem(&mtab->types, &sb.dev, sizeof(sb.dev));
		if (leaf) {
			leaf->value = entry->type;
		} else {
			goto fail;
		}
	}

	mtab->types_filled = true;
	ret = 0;

fail:
	if (parent_fd >= 0) {
		xclose(parent_fd);
	}
	free(parent_dir);
	return ret;
}

const char *bfs_fstype(const struct bfs_mtab *mtab, const struct bfs_stat *statbuf) {
	if (!mtab->types_filled) {
		if (bfs_mtab_fill_types((struct bfs_mtab *)mtab) != 0) {
			return NULL;
		}
	}

	const struct trie_leaf *leaf = trie_find_mem(&mtab->types, &statbuf->dev, sizeof(statbuf->dev));
	if (leaf) {
		return leaf->value;
	} else {
		return "unknown";
	}
}

bool bfs_might_be_mount(const struct bfs_mtab *mtab, const char *path) {
	const char *name = path + xbaseoff(path);
	return trie_find_str(&mtab->names, name);
}

void bfs_mtab_free(struct bfs_mtab *mtab) {
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
