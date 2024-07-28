// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include "prelude.h"
#include "mtab.h"
#include "alloc.h"
#include "bfstd.h"
#include "stat.h"
#include "trie.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#if !defined(BFS_USE_MNTENT) && BFS_HAS_GETMNTENT_1
#  define BFS_USE_MNTENT true
#elif !defined(BFS_USE_MNTINFO) && BFS_HAS_GETMNTINFO
#  define BFS_USE_MNTINFO true
#elif !defined(BFS_USE_MNTTAB) && BFS_HAS_GETMNTENT_2
#  define BFS_USE_MNTTAB true
#endif

#if BFS_USE_MNTENT
#  include <mntent.h>
#  include <paths.h>
#  include <stdio.h>
#elif BFS_USE_MNTINFO
#  include <sys/mount.h>
#elif BFS_USE_MNTTAB
#  include <stdio.h>
#  include <sys/mnttab.h>
#endif

/**
 * A mount point in the table.
 */
struct bfs_mount {
	/** The path to the mount point. */
	char *path;
	/** The filesystem type. */
	char *type;
	/** Buffer for the strings. */
	char buf[];
};

struct bfs_mtab {
	/** Mount point arena. */
	struct varena varena;

	/** The array of mount points. */
	struct bfs_mount **mounts;
	/** The number of mount points. */
	size_t nmounts;

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
_maybe_unused
static int bfs_mtab_add(struct bfs_mtab *mtab, const char *path, const char *type) {
	size_t path_size = strlen(path) + 1;
	size_t type_size = strlen(type) + 1;
	size_t size = path_size + type_size;
	struct bfs_mount *mount = varena_alloc(&mtab->varena, size);
	if (!mount) {
		return -1;
	}

	struct bfs_mount **ptr = RESERVE(struct bfs_mount *, &mtab->mounts, &mtab->nmounts);
	if (!ptr) {
		goto free;
	}
	*ptr = mount;

	mount->path = mount->buf;
	memcpy(mount->path, path, path_size);

	mount->type = mount->buf + path_size;
	memcpy(mount->type, type, type_size);

	const char *name = path + xbaseoff(path);
	if (!trie_insert_str(&mtab->names, name)) {
		goto shrink;
	}

	return 0;

shrink:
	--mtab->nmounts;
free:
	varena_free(&mtab->varena, mount, size);
	return -1;
}

struct bfs_mtab *bfs_mtab_parse(void) {
	struct bfs_mtab *mtab = ZALLOC(struct bfs_mtab);
	if (!mtab) {
		return NULL;
	}

	VARENA_INIT(&mtab->varena, struct bfs_mount, buf);

	trie_init(&mtab->names);
	trie_init(&mtab->types);

	int error = 0;

#if BFS_USE_MNTENT

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

#elif BFS_USE_MNTINFO

#if __NetBSD__
	typedef struct statvfs bfs_statfs;
#else
	typedef struct statfs bfs_statfs;
#endif

	bfs_statfs *mntbuf;
	int size = getmntinfo(&mntbuf, MNT_WAIT);
	if (size <= 0) {
		error = errno;
		goto fail;
	}

	for (bfs_statfs *mnt = mntbuf; mnt < mntbuf + size; ++mnt) {
		if (bfs_mtab_add(mtab, mnt->f_mntonname, mnt->f_fstypename) != 0) {
			error = errno;
			goto fail;
		}
	}

#elif BFS_USE_MNTTAB

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
	int parent_ret = -1;
	struct bfs_stat parent_stat;

	for (size_t i = 0; i < mtab->nmounts; ++i) {
		struct bfs_mount *mount = mtab->mounts[i];
		const char *path = mount->path;
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
			leaf->value = mount->type;
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

bool bfs_might_be_mount(const struct bfs_mtab *mtab, const char *name) {
	return trie_find_str(&mtab->names, name);
}

void bfs_mtab_free(struct bfs_mtab *mtab) {
	if (mtab) {
		trie_destroy(&mtab->types);
		trie_destroy(&mtab->names);

		free(mtab->mounts);
		varena_destroy(&mtab->varena);

		free(mtab);
	}
}
