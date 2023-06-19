// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

/**
 * The bftw() implementation consists of the following components:
 *
 * - struct bftw_file: A file that has been encountered during the traversal.
 *   They have reference-counted links to their parents in the directory tree.
 *
 * - struct bftw_list: A linked list of bftw_file's.
 *
 * - struct bftw_cache: An LRU list of bftw_file's with open file descriptors,
 *   used for openat() to minimize the amount of path re-traversals.
 *
 * - struct bftw_state: Represents the current state of the traversal, allowing
 *   various helper functions to take fewer parameters.
 */

#include "bftw.h"
#include "alloc.h"
#include "bfstd.h"
#include "config.h"
#include "diag.h"
#include "dir.h"
#include "dstring.h"
#include "ioq.h"
#include "list.h"
#include "mtab.h"
#include "stat.h"
#include "trie.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/**
 * A file.
 */
struct bftw_file {
	/** The parent directory, if any. */
	struct bftw_file *parent;
	/** The root under which this file was found. */
	struct bftw_file *root;
	/** The next file in the queue, if any. */
	struct bftw_file *next;

	/** LRU list links. */
	struct {
		struct bftw_file *prev;
		struct bftw_file *next;
	} lru;

	/** This file's depth in the walk. */
	size_t depth;
	/** Reference count (for ->parent). */
	size_t refcount;

	/** Pin count (for ->fd). */
	size_t pincount;
	/** An open descriptor to this file, or -1. */
	int fd;
	/** Whether this file has a pending ioq request. */
	bool ioqueued;
	/** An open directory for this file, if any. */
	struct bfs_dir *dir;

	/** This file's type, if known. */
	enum bfs_type type;
	/** The device number, for cycle detection. */
	dev_t dev;
	/** The inode number, for cycle detection. */
	ino_t ino;

	/** The offset of this file in the full path. */
	size_t nameoff;
	/** The length of the file's name. */
	size_t namelen;
	/** The file's name. */
	char name[];
};

/**
 * A linked list of bftw_file's.
 */
struct bftw_list {
	struct bftw_file *head;
	struct bftw_file **tail;
};

/**
 * A cache of open directories.
 */
struct bftw_cache {
	/** The head of the LRU list. */
	struct bftw_file *head;
	/** The tail of the LRU list. */
	struct bftw_file *tail;
	/** The insertion target for the LRU list. */
	struct bftw_file *target;
	/** The remaining capacity of the LRU list. */
	size_t capacity;
};

/** Initialize a cache. */
static void bftw_cache_init(struct bftw_cache *cache, size_t capacity) {
	LIST_INIT(cache);
	cache->target = NULL;
	cache->capacity = capacity;
}

/** Remove a bftw_file from the LRU list. */
static void bftw_lru_remove(struct bftw_cache *cache, struct bftw_file *file) {
	if (cache->target == file) {
		cache->target = file->lru.prev;
	}

	LIST_REMOVE(cache, file, lru);
}

/** Remove a bftw_file from the cache. */
static void bftw_cache_remove(struct bftw_cache *cache, struct bftw_file *file) {
	bftw_lru_remove(cache, file);
	++cache->capacity;
}

/** Close a bftw_file. */
static void bftw_file_close(struct bftw_cache *cache, struct bftw_file *file) {
	bfs_assert(file->fd >= 0);
	bfs_assert(file->pincount == 0);

	if (file->dir) {
		bfs_assert(file->fd == bfs_dirfd(file->dir));
		bfs_closedir(file->dir);
		file->dir = NULL;
	} else {
		xclose(file->fd);
	}

	file->fd = -1;
	bftw_cache_remove(cache, file);
}

/** Free an open directory. */
static void bftw_file_freedir(struct bftw_cache *cache, struct bftw_file *file) {
	if (!file->dir) {
		return;
	}

	// Try to keep an open fd if any children exist
	bool reffed = file->refcount > 1;
	// Keep the fd the same if it's pinned
	bool pinned = file->pincount > 0;

	if (reffed || pinned) {
		int fd = bfs_freedir(file->dir, pinned);
		if (fd >= 0) {
			file->fd = fd;
			file->dir = NULL;
		}
	} else {
		bftw_file_close(cache, file);
	}
}

/** Pop the least recently used directory from the cache. */
static int bftw_cache_pop(struct bftw_cache *cache) {
	struct bftw_file *file = cache->tail;
	if (!file) {
		return -1;
	}

	bftw_file_close(cache, file);
	return 0;
}

/** Add a bftw_file to the LRU list. */
static void bftw_lru_add(struct bftw_cache *cache, struct bftw_file *file) {
	bfs_assert(file->fd >= 0);

	LIST_INSERT(cache, cache->target, file, lru);

	// Prefer to keep the root paths open by keeping them at the head of the list
	if (file->depth == 0) {
		cache->target = file;
	}
}

/** Add a bftw_file to the cache. */
static int bftw_cache_add(struct bftw_cache *cache, struct bftw_file *file) {
	bfs_assert(file->fd >= 0);

	if (cache->capacity == 0 && bftw_cache_pop(cache) != 0) {
		bftw_file_close(cache, file);
		errno = EMFILE;
		return -1;
	}

	bfs_assert(cache->capacity > 0);
	--cache->capacity;

	bftw_lru_add(cache, file);
	return 0;
}

/** Pin a cache entry so it won't be closed. */
static void bftw_cache_pin(struct bftw_cache *cache, struct bftw_file *file) {
	bfs_assert(file->fd >= 0);

	if (file->pincount++ == 0) {
		bftw_lru_remove(cache, file);
	}
}

/** Unpin a cache entry. */
static void bftw_cache_unpin(struct bftw_cache *cache, struct bftw_file *file) {
	bfs_assert(file->fd >= 0);
	bfs_assert(file->pincount > 0);

	if (--file->pincount == 0) {
		bftw_lru_add(cache, file);
		bftw_file_freedir(cache, file);
	}
}

/** Compute the name offset of a child path. */
static size_t bftw_child_nameoff(const struct bftw_file *parent) {
	size_t ret = parent->nameoff + parent->namelen;
	if (parent->name[parent->namelen - 1] != '/') {
		++ret;
	}
	return ret;
}

/** Destroy a cache. */
static void bftw_cache_destroy(struct bftw_cache *cache) {
	bfs_assert(!cache->head);
	bfs_assert(!cache->tail);
	bfs_assert(!cache->target);
}

/** Create a new bftw_file. */
static struct bftw_file *bftw_file_new(struct bftw_file *parent, const char *name) {
	size_t namelen = strlen(name);
	struct bftw_file *file = ALLOC_FLEX(struct bftw_file, name, namelen + 1);
	if (!file) {
		return NULL;
	}

	file->parent = parent;

	if (parent) {
		file->root = parent->root;
		file->depth = parent->depth + 1;
		file->nameoff = bftw_child_nameoff(parent);
		++parent->refcount;
	} else {
		file->root = file;
		file->depth = 0;
		file->nameoff = 0;
	}

	file->next = NULL;
	file->lru.prev = file->lru.next = NULL;

	file->refcount = 1;
	file->pincount = 0;
	file->fd = -1;
	file->ioqueued = false;
	file->dir = NULL;

	file->type = BFS_UNKNOWN;
	file->dev = -1;
	file->ino = -1;

	file->namelen = namelen;
	memcpy(file->name, name, namelen + 1);

	return file;
}

/**
 * Open a bftw_file relative to another one.
 *
 * @param cache
 *         The cache to hold the file.
 * @param file
 *         The file to open.
 * @param base
 *         The base directory for the relative path (may be NULL).
 * @param at_fd
 *         The base file descriptor, AT_FDCWD if base == NULL.
 * @param at_path
 *         The relative path to the file.
 * @return
 *         The opened file descriptor, or negative on error.
 */
static int bftw_file_openat(struct bftw_cache *cache, struct bftw_file *file, struct bftw_file *base, const char *at_path) {
	bfs_assert(file->fd < 0);

	int at_fd = AT_FDCWD;
	if (base) {
		bftw_cache_pin(cache, base);
		at_fd = base->fd;
	}

	int flags = O_RDONLY | O_CLOEXEC | O_DIRECTORY;
	int fd = openat(at_fd, at_path, flags);

	if (fd < 0 && errno == EMFILE) {
		if (bftw_cache_pop(cache) == 0) {
			fd = openat(at_fd, at_path, flags);
		}
		cache->capacity = 1;
	}

	if (base) {
		bftw_cache_unpin(cache, base);
	}

	if (fd >= 0) {
		file->fd = fd;
		bftw_cache_add(cache, file);
	}

	return file->fd;
}

/**
 * Open a bftw_file.
 *
 * @param cache
 *         The cache to hold the file.
 * @param file
 *         The file to open.
 * @param path
 *         The full path to the file.
 * @return
 *         The opened file descriptor, or negative on error.
 */
static int bftw_file_open(struct bftw_cache *cache, struct bftw_file *file, const char *path) {
	// Find the nearest open ancestor
	struct bftw_file *base = file;
	do {
		base = base->parent;
	} while (base && base->fd < 0);

	const char *at_path = path;
	if (base) {
		at_path += bftw_child_nameoff(base);
	}

	int fd = bftw_file_openat(cache, file, base, at_path);
	if (fd >= 0 || errno != ENAMETOOLONG) {
		return fd;
	}

	// Handle ENAMETOOLONG by manually traversing the path component-by-component
	struct bftw_list parents;
	SLIST_INIT(&parents);

	struct bftw_file *cur;
	for (cur = file; cur != base; cur = cur->parent) {
		SLIST_PREPEND(&parents, cur);
	}

	while ((cur = SLIST_POP(&parents))) {
		if (!cur->parent || cur->parent->fd >= 0) {
			bftw_file_openat(cache, cur, cur->parent, cur->name);
		}
	}

	return file->fd;
}

/**
 * Associate an open directory with a bftw_file.
 */
static void bftw_file_set_dir(struct bftw_cache *cache, struct bftw_file *file, struct bfs_dir *dir) {
	bfs_assert(!file->dir);
	file->dir = dir;

	if (file->fd < 0) {
		file->fd = bfs_dirfd(dir);
		bftw_cache_add(cache, file);
	}
}

/**
 * Open a bftw_file as a directory.
 *
 * @param cache
 *         The cache to hold the file.
 * @param file
 *         The directory to open.
 * @param path
 *         The full path to the directory.
 * @return
 *         The opened directory, or NULL on error.
 */
static struct bfs_dir *bftw_file_opendir(struct bftw_cache *cache, struct bftw_file *file, const char *path) {
	int fd = bftw_file_open(cache, file, path);
	if (fd < 0) {
		return NULL;
	}

	struct bfs_dir *dir = bfs_opendir(fd, NULL);
	if (dir) {
		bftw_file_set_dir(cache, file, dir);
	}
	return dir;
}

/** Free a bftw_file. */
static void bftw_file_free(struct bftw_cache *cache, struct bftw_file *file) {
	bfs_assert(file->refcount == 0);

	if (file->fd >= 0) {
		bftw_file_close(cache, file);
	}

	free(file);
}

/**
 * Holds the current state of the bftw() traversal.
 */
struct bftw_state {
	/** bftw() callback. */
	bftw_callback *callback;
	/** bftw() callback data. */
	void *ptr;
	/** bftw() flags. */
	enum bftw_flags flags;
	/** Search strategy. */
	enum bftw_strategy strategy;
	/** The mount table. */
	const struct bfs_mtab *mtab;

	/** The appropriate errno value, if any. */
	int error;

	/** The cache of open directories. */
	struct bftw_cache cache;
	/** The async I/O queue. */
	struct ioq *ioq;
	/** The queue of directories to read. */
	struct bftw_list dirs;
	/** The queue of files to visit. */
	struct bftw_list files;
	/** A batch of files to enqueue. */
	struct bftw_list batch;

	/** The current path. */
	char *path;
	/** The current file. */
	struct bftw_file *file;
	/** The previous file. */
	struct bftw_file *previous;

	/** The currently open directory. */
	struct bfs_dir *dir;
	/** The current directory entry. */
	struct bfs_dirent *de;
	/** Storage for the directory entry. */
	struct bfs_dirent de_storage;
	/** Any error encountered while reading the directory. */
	int direrror;

	/** Extra data about the current file. */
	struct BFTW ftwbuf;
};

/**
 * Initialize the bftw() state.
 */
static int bftw_state_init(struct bftw_state *state, const struct bftw_args *args) {
	state->callback = args->callback;
	state->ptr = args->ptr;
	state->flags = args->flags;
	state->strategy = args->strategy;
	state->mtab = args->mtab;

	if ((state->flags & BFTW_SORT) || state->strategy == BFTW_DFS) {
		state->flags |= BFTW_BUFFER;
	}

	state->error = 0;

	if (args->nopenfd < 1) {
		errno = EMFILE;
		return -1;
	}

	state->path = dstralloc(0);
	if (!state->path) {
		return -1;
	}

	bftw_cache_init(&state->cache, args->nopenfd);

	size_t qdepth = args->nopenfd - 1;
	if (qdepth > 1024) {
		qdepth = 1024;
	}

	size_t nthreads = args->nthreads;
	if (nthreads > qdepth) {
		nthreads = qdepth;
	}

	state->ioq = NULL;
	if (nthreads > 0) {
		state->ioq = ioq_create(qdepth, nthreads);
		if (!state->ioq) {
			dstrfree(state->path);
			return -1;
		}
	}

	SLIST_INIT(&state->dirs);
	SLIST_INIT(&state->files);
	SLIST_INIT(&state->batch);

	state->file = NULL;
	state->previous = NULL;

	state->dir = NULL;
	state->de = NULL;
	state->direrror = 0;

	return 0;
}

/** Cached bfs_stat(). */
static const struct bfs_stat *bftw_stat_impl(struct BFTW *ftwbuf, struct bftw_stat *cache, enum bfs_stat_flags flags) {
	if (!cache->buf) {
		if (cache->error) {
			errno = cache->error;
		} else if (bfs_stat(ftwbuf->at_fd, ftwbuf->at_path, flags, &cache->storage) == 0) {
			cache->buf = &cache->storage;
		} else {
			cache->error = errno;
		}
	}

	return cache->buf;
}

const struct bfs_stat *bftw_stat(const struct BFTW *ftwbuf, enum bfs_stat_flags flags) {
	struct BFTW *mutbuf = (struct BFTW *)ftwbuf;
	const struct bfs_stat *ret;

	if (flags & BFS_STAT_NOFOLLOW) {
		ret = bftw_stat_impl(mutbuf, &mutbuf->lstat_cache, BFS_STAT_NOFOLLOW);
		if (ret && !S_ISLNK(ret->mode) && !mutbuf->stat_cache.buf) {
			// Non-link, so share stat info
			mutbuf->stat_cache.buf = ret;
		}
	} else {
		ret = bftw_stat_impl(mutbuf, &mutbuf->stat_cache, BFS_STAT_FOLLOW);
		if (!ret && (flags & BFS_STAT_TRYFOLLOW) && is_nonexistence_error(errno)) {
			ret = bftw_stat_impl(mutbuf, &mutbuf->lstat_cache, BFS_STAT_NOFOLLOW);
		}
	}

	return ret;
}

const struct bfs_stat *bftw_cached_stat(const struct BFTW *ftwbuf, enum bfs_stat_flags flags) {
	if (flags & BFS_STAT_NOFOLLOW) {
		return ftwbuf->lstat_cache.buf;
	} else if (ftwbuf->stat_cache.buf) {
		return ftwbuf->stat_cache.buf;
	} else if ((flags & BFS_STAT_TRYFOLLOW) && is_nonexistence_error(ftwbuf->stat_cache.error)) {
		return ftwbuf->lstat_cache.buf;
	} else {
		return NULL;
	}
}

enum bfs_type bftw_type(const struct BFTW *ftwbuf, enum bfs_stat_flags flags) {
	if (flags & BFS_STAT_NOFOLLOW) {
		if (ftwbuf->type == BFS_LNK || (ftwbuf->stat_flags & BFS_STAT_NOFOLLOW)) {
			return ftwbuf->type;
		}
	} else if (flags & BFS_STAT_TRYFOLLOW) {
		if (ftwbuf->type != BFS_LNK || (ftwbuf->stat_flags & BFS_STAT_TRYFOLLOW)) {
			return ftwbuf->type;
		}
	} else {
		if (ftwbuf->type != BFS_LNK) {
			return ftwbuf->type;
		} else if (ftwbuf->stat_flags & BFS_STAT_TRYFOLLOW) {
			return BFS_ERROR;
		}
	}

	const struct bfs_stat *statbuf = bftw_stat(ftwbuf, flags);
	if (statbuf) {
		return bfs_mode_to_type(statbuf->mode);
	} else {
		return BFS_ERROR;
	}
}

/**
 * Build the path to the current file.
 */
static int bftw_build_path(struct bftw_state *state, const char *name) {
	const struct bftw_file *file = state->file;

	size_t pathlen = file ? file->nameoff + file->namelen : 0;
	if (dstresize(&state->path, pathlen) != 0) {
		state->error = errno;
		return -1;
	}

	// Try to find a common ancestor with the existing path
	const struct bftw_file *ancestor = state->previous;
	while (ancestor && ancestor->depth > file->depth) {
		ancestor = ancestor->parent;
	}

	// Build the path backwards
	while (file && file != ancestor) {
		if (file->nameoff > 0) {
			state->path[file->nameoff - 1] = '/';
		}
		memcpy(state->path + file->nameoff, file->name, file->namelen);

		if (ancestor && ancestor->depth == file->depth) {
			ancestor = ancestor->parent;
		}
		file = file->parent;
	}

	state->previous = state->file;

	if (name) {
		if (pathlen > 0 && state->path[pathlen - 1] != '/') {
			if (dstrapp(&state->path, '/') != 0) {
				state->error = errno;
				return -1;
			}
		}
		if (dstrcat(&state->path, name) != 0) {
			state->error = errno;
			return -1;
		}
	}

	return 0;
}

/** Check if a stat() call is needed for this visit. */
static bool bftw_need_stat(const struct bftw_state *state) {
	if (state->flags & BFTW_STAT) {
		return true;
	}

	const struct BFTW *ftwbuf = &state->ftwbuf;
	if (ftwbuf->type == BFS_UNKNOWN) {
		return true;
	}

	if (ftwbuf->type == BFS_LNK && !(ftwbuf->stat_flags & BFS_STAT_NOFOLLOW)) {
		return true;
	}

	if (ftwbuf->type == BFS_DIR) {
		if (state->flags & (BFTW_DETECT_CYCLES | BFTW_SKIP_MOUNTS | BFTW_PRUNE_MOUNTS)) {
			return true;
		}
#if __linux__
	} else if (state->mtab) {
		// Linux fills in d_type from the underlying inode, even when
		// the directory entry is a bind mount point.  In that case, we
		// need to stat() to get the correct type.  We don't need to
		// check for directories because they can only be mounted over
		// by other directories.
		if (bfs_might_be_mount(state->mtab, ftwbuf->path)) {
			return true;
		}
#endif
	}

	return false;
}

/** Initialize bftw_stat cache. */
static void bftw_stat_init(struct bftw_stat *cache) {
	cache->buf = NULL;
	cache->error = 0;
}

/**
 * Open a file if necessary.
 *
 * @param file
 *         The file to open.
 * @param path
 *         The path to that file or one of its descendants.
 * @return
 *         The opened file descriptor, or -1 on error.
 */
static int bftw_ensure_open(struct bftw_cache *cache, struct bftw_file *file, const char *path) {
	int ret = file->fd;

	if (ret < 0) {
		char *copy = strndup(path, file->nameoff + file->namelen);
		if (!copy) {
			return -1;
		}

		ret = bftw_file_open(cache, file, copy);
		free(copy);
	}

	return ret;
}

/**
 * Initialize the buffers with data about the current path.
 */
static void bftw_init_ftwbuf(struct bftw_state *state, enum bftw_visit visit) {
	struct bftw_file *file = state->file;
	const struct bfs_dirent *de = state->de;

	struct BFTW *ftwbuf = &state->ftwbuf;
	ftwbuf->path = state->path;
	ftwbuf->root = file ? file->root->name : ftwbuf->path;
	ftwbuf->depth = 0;
	ftwbuf->visit = visit;
	ftwbuf->type = BFS_UNKNOWN;
	ftwbuf->error = state->direrror;
	ftwbuf->at_fd = AT_FDCWD;
	ftwbuf->at_path = ftwbuf->path;
	ftwbuf->stat_flags = BFS_STAT_NOFOLLOW;
	bftw_stat_init(&ftwbuf->lstat_cache);
	bftw_stat_init(&ftwbuf->stat_cache);

	struct bftw_file *parent = NULL;
	if (de) {
		parent = file;
		ftwbuf->depth = file->depth + 1;
		ftwbuf->type = de->type;
		ftwbuf->nameoff = bftw_child_nameoff(file);
	} else if (file) {
		parent = file->parent;
		ftwbuf->depth = file->depth;
		ftwbuf->type = file->type;
		ftwbuf->nameoff = file->nameoff;
	}

	if (parent) {
		// Try to ensure the immediate parent is open, to avoid ENAMETOOLONG
		if (bftw_ensure_open(&state->cache, parent, state->path) >= 0) {
			ftwbuf->at_fd = parent->fd;
			ftwbuf->at_path += ftwbuf->nameoff;
		} else {
			ftwbuf->error = errno;
		}
	}

	if (ftwbuf->depth == 0) {
		// Compute the name offset for root paths like "foo/bar"
		ftwbuf->nameoff = xbaseoff(ftwbuf->path);
	}

	if (ftwbuf->error != 0) {
		ftwbuf->type = BFS_ERROR;
		return;
	}

	int follow_flags = BFTW_FOLLOW_ALL;
	if (ftwbuf->depth == 0) {
		follow_flags |= BFTW_FOLLOW_ROOTS;
	}
	bool follow = state->flags & follow_flags;
	if (follow) {
		ftwbuf->stat_flags = BFS_STAT_TRYFOLLOW;
	}

	const struct bfs_stat *statbuf = NULL;
	if (bftw_need_stat(state)) {
		statbuf = bftw_stat(ftwbuf, ftwbuf->stat_flags);
		if (statbuf) {
			ftwbuf->type = bfs_mode_to_type(statbuf->mode);
		} else {
			ftwbuf->type = BFS_ERROR;
			ftwbuf->error = errno;
			return;
		}
	}

	if (ftwbuf->type == BFS_DIR && (state->flags & BFTW_DETECT_CYCLES)) {
		for (const struct bftw_file *ancestor = parent; ancestor; ancestor = ancestor->parent) {
			if (ancestor->dev == statbuf->dev && ancestor->ino == statbuf->ino) {
				ftwbuf->type = BFS_ERROR;
				ftwbuf->error = ELOOP;
				return;
			}
		}
	}
}

/** Check if the current file is a mount point. */
static bool bftw_is_mount(struct bftw_state *state, const char *name) {
	const struct bftw_file *file = state->file;
	if (!file) {
		return false;
	}

	const struct bftw_file *parent = name ? file : file->parent;
	if (!parent) {
		return false;
	}

	const struct BFTW *ftwbuf = &state->ftwbuf;
	const struct bfs_stat *statbuf = bftw_stat(ftwbuf, ftwbuf->stat_flags);
	return statbuf && statbuf->dev != parent->dev;
}

/**
 * Invoke the callback.
 */
static enum bftw_action bftw_call_back(struct bftw_state *state, const char *name, enum bftw_visit visit) {
	if (visit == BFTW_POST && !(state->flags & BFTW_POST_ORDER)) {
		return BFTW_PRUNE;
	}

	if (bftw_build_path(state, name) != 0) {
		return BFTW_STOP;
	}

	const struct BFTW *ftwbuf = &state->ftwbuf;
	bftw_init_ftwbuf(state, visit);

	// Never give the callback BFS_ERROR unless BFTW_RECOVER is specified
	if (ftwbuf->type == BFS_ERROR && !(state->flags & BFTW_RECOVER)) {
		state->error = ftwbuf->error;
		return BFTW_STOP;
	}

	if ((state->flags & BFTW_SKIP_MOUNTS) && bftw_is_mount(state, name)) {
		return BFTW_PRUNE;
	}

	enum bftw_action ret = state->callback(ftwbuf, state->ptr);
	switch (ret) {
	case BFTW_CONTINUE:
		if (visit != BFTW_PRE) {
			return BFTW_PRUNE;
		}
		if (ftwbuf->type != BFS_DIR) {
			return BFTW_PRUNE;
		}
		if ((state->flags & BFTW_PRUNE_MOUNTS) && bftw_is_mount(state, name)) {
			return BFTW_PRUNE;
		}
		fallthru;
	case BFTW_PRUNE:
	case BFTW_STOP:
		return ret;

	default:
		state->error = EINVAL;
		return BFTW_STOP;
	}
}

/** Push a directory onto the queue. */
static void bftw_push_dir(struct bftw_state *state, struct bftw_file *file) {
	bfs_assert(file->type == BFS_DIR);

	struct bftw_cache *cache = &state->cache;

	if (!state->ioq) {
		goto append;
	}

	int dfd = AT_FDCWD;
	if (file->parent) {
		dfd = file->parent->fd;
		if (dfd < 0) {
			goto append;
		}
		bftw_cache_pin(cache, file->parent);
	}

	if (cache->capacity == 0) {
		if (bftw_cache_pop(cache) != 0) {
			goto unpin;
		}
	}
	--cache->capacity;

	if (ioq_opendir(state->ioq, dfd, file->name, file) != 0) {
		++cache->capacity;
		goto unpin;
	}

	file->ioqueued = true;

	if (state->flags & BFTW_SORT) {
		goto append;
	} else {
		return;
	}

unpin:
	if (file->parent) {
		bftw_cache_unpin(cache, file->parent);
	}
append:
	SLIST_APPEND(&state->dirs, file);
}

/** Pop a response from the I/O queue. */
static int bftw_ioq_pop(struct bftw_state *state, bool block) {
	if (!state->ioq) {
		return -1;
	}

	struct ioq_res *res;
	if (block) {
		res = ioq_pop(state->ioq);
	} else {
		res = ioq_trypop(state->ioq);
	}

	if (!res) {
		return -1;
	}

	struct bftw_cache *cache = &state->cache;
	++cache->capacity;

	struct bftw_file *file = res->ptr;
	file->ioqueued = false;

	if (file->parent) {
		bftw_cache_unpin(cache, file->parent);
	}

	if (res->dir) {
		bftw_file_set_dir(cache, file, res->dir);
	}

	ioq_free(state->ioq, res);

	if (!(state->flags & BFTW_SORT)) {
		SLIST_PREPEND(&state->dirs, file);
	}

	return 0;
}

/** Pop a directory to read from the queue. */
static bool bftw_pop_dir(struct bftw_state *state) {
	bfs_assert(!state->file);

	bool have_dirs = state->dirs.head;
	bool have_files = state->files.head;
	bool have_room = state->cache.capacity > 0;

	if (state->flags & BFTW_SORT) {
		// Keep strict breadth-first order when sorting
		if (state->strategy != BFTW_DFS && have_files) {
			return false;
		}
	} else {
		// Block if we have no other files/dirs to visit, or no room in the cache
		bool block = !(have_dirs || have_files) || !have_room;
		bftw_ioq_pop(state, block);
	}

	struct bftw_file *dir = state->file = SLIST_POP(&state->dirs);
	if (!dir) {
		return false;
	}

	while (dir->ioqueued) {
		bftw_ioq_pop(state, true);
	}

	return true;
}

/** Pop a file to visit from the queue. */
static bool bftw_pop_file(struct bftw_state *state) {
	bfs_assert(!state->file);
	return (state->file = SLIST_POP(&state->files));
}

/**
 * Open the current directory.
 */
static int bftw_opendir(struct bftw_state *state) {
	bfs_assert(!state->dir);
	bfs_assert(!state->de);

	state->direrror = 0;

	struct bftw_file *file = state->file;
	if (file->dir) {
		state->dir = file->dir;
	} else {
		if (bftw_build_path(state, NULL) != 0) {
			return -1;
		}
		state->dir = bftw_file_opendir(&state->cache, file, state->path);
	}

	if (state->dir) {
		bftw_cache_pin(&state->cache, file);
	} else {
		state->direrror = errno;
	}

	return 0;
}

/**
 * Read an entry from the current directory.
 */
static int bftw_readdir(struct bftw_state *state) {
	if (!state->dir) {
		return -1;
	}

	int ret = bfs_readdir(state->dir, &state->de_storage);
	if (ret > 0) {
		state->de = &state->de_storage;
	} else if (ret == 0) {
		state->de = NULL;
	} else {
		state->de = NULL;
		state->direrror = errno;
	}

	return ret;
}

/**
 * Flags controlling which files get visited when done with a directory.
 */
enum bftw_gc_flags {
	/** Don't visit anything. */
	BFTW_VISIT_NONE = 0,
	/** Report directory errors. */
	BFTW_VISIT_ERROR = 1 << 0,
	/** Visit the file itself. */
	BFTW_VISIT_FILE = 1 << 1,
	/** Visit the file's ancestors. */
	BFTW_VISIT_PARENTS = 1 << 2,
	/** Visit both the file and its ancestors. */
	BFTW_VISIT_ALL = BFTW_VISIT_ERROR | BFTW_VISIT_FILE | BFTW_VISIT_PARENTS,
};

/**
 * Garbage collect the current file and its parents.
 */
static int bftw_gc(struct bftw_state *state, enum bftw_gc_flags flags) {
	int ret = 0;

	if (state->dir) {
		bftw_cache_unpin(&state->cache, state->file);
		bftw_file_freedir(&state->cache, state->file);
	}
	state->dir = NULL;
	state->de = NULL;

	if (state->direrror != 0) {
		if (flags & BFTW_VISIT_ERROR) {
			if (bftw_call_back(state, NULL, BFTW_PRE) == BFTW_STOP) {
				ret = -1;
				flags = 0;
			}
		} else {
			state->error = state->direrror;
		}
	}
	state->direrror = 0;

	enum bftw_gc_flags visit = BFTW_VISIT_FILE;
	while (state->file) {
		struct bftw_file *file = state->file;
		if (--file->refcount > 0) {
			state->file = NULL;
			break;
		}

		if (flags & visit) {
			if (bftw_call_back(state, NULL, BFTW_POST) == BFTW_STOP) {
				ret = -1;
				flags = 0;
			}
		}
		visit = BFTW_VISIT_PARENTS;

		struct bftw_file *parent = file->parent;
		if (state->previous == file) {
			state->previous = parent;
		}
		bftw_file_free(&state->cache, file);
		state->file = parent;
	}

	return ret;
}

/**
 * Dispose of the bftw() state.
 *
 * @return
 *         The bftw() return value.
 */
static int bftw_state_destroy(struct bftw_state *state) {
	dstrfree(state->path);

	SLIST_EXTEND(&state->files, &state->batch);
	do {
		bftw_gc(state, BFTW_VISIT_NONE);
	} while (bftw_pop_dir(state) || bftw_pop_file(state));

	ioq_destroy(state->ioq);

	bftw_cache_destroy(&state->cache);

	errno = state->error;
	return state->error ? -1 : 0;
}

/** Sort a bftw_list by filename. */
static void bftw_list_sort(struct bftw_list *list) {
	if (!list->head || !list->head->next) {
		return;
	}

	struct bftw_list left, right;
	SLIST_INIT(&left);
	SLIST_INIT(&right);

	// Split
	for (struct bftw_file *hare = list->head; hare && (hare = hare->next); hare = hare->next) {
		struct bftw_file *tortoise = SLIST_POP(list);
		SLIST_APPEND(&left, tortoise);
	}
	SLIST_EXTEND(&right, list);

	// Recurse
	bftw_list_sort(&left);
	bftw_list_sort(&right);

	// Merge
	while (left.head && right.head) {
		struct bftw_file *lf = left.head;
		struct bftw_file *rf = right.head;

		if (strcoll(lf->name, rf->name) <= 0) {
			SLIST_POP(&left);
			SLIST_APPEND(list, lf);
		} else {
			SLIST_POP(&right);
			SLIST_APPEND(list, rf);
		}
	}
	SLIST_EXTEND(list, &left);
	SLIST_EXTEND(list, &right);
}

/** Finish adding a batch of files. */
static void bftw_batch_finish(struct bftw_state *state) {
	if (state->flags & BFTW_SORT) {
		bftw_list_sort(&state->batch);
	}

	if (state->strategy != BFTW_BFS) {
		SLIST_EXTEND(&state->batch, &state->files);
	}
	SLIST_EXTEND(&state->files, &state->batch);
}

/** Close the current directory. */
static int bftw_closedir(struct bftw_state *state) {
	if (bftw_gc(state, BFTW_VISIT_ALL) != 0) {
		return -1;
	}

	bftw_batch_finish(state);
	return 0;
}

/** Fill file identity information from an ftwbuf. */
static void bftw_save_ftwbuf(struct bftw_file *file, const struct BFTW *ftwbuf) {
	file->type = ftwbuf->type;

	const struct bfs_stat *statbuf = ftwbuf->stat_cache.buf;
	if (!statbuf || (ftwbuf->stat_flags & BFS_STAT_NOFOLLOW)) {
		statbuf = ftwbuf->lstat_cache.buf;
	}
	if (statbuf) {
		file->dev = statbuf->dev;
		file->ino = statbuf->ino;
	}
}

/** Visit and/or enqueue the current file. */
static int bftw_visit(struct bftw_state *state, const char *name) {
	struct bftw_file *file = state->file;

	if (name && (state->flags & BFTW_BUFFER)) {
		file = bftw_file_new(file, name);
		if (!file) {
			state->error = errno;
			return -1;
		}

		if (state->de) {
			file->type = state->de->type;
		}

		SLIST_APPEND(&state->batch, file);
		return 0;
	}

	switch (bftw_call_back(state, name, BFTW_PRE)) {
	case BFTW_CONTINUE:
		if (name) {
			file = bftw_file_new(state->file, name);
		} else {
			state->file = NULL;
		}
		if (!file) {
			state->error = errno;
			return -1;
		}

		bftw_save_ftwbuf(file, &state->ftwbuf);
		bftw_push_dir(state, file);
		return 0;

	case BFTW_PRUNE:
		if (file && !name) {
			return bftw_gc(state, BFTW_VISIT_PARENTS);
		} else {
			return 0;
		}

	default:
		return -1;
	}
}

/**
 * bftw() implementation for simple breadth-/depth-first search.
 */
static int bftw_impl(const struct bftw_args *args) {
	struct bftw_state state;
	if (bftw_state_init(&state, args) != 0) {
		return -1;
	}

	for (size_t i = 0; i < args->npaths; ++i) {
		if (bftw_visit(&state, args->paths[i]) != 0) {
			goto done;
		}
	}
	bftw_batch_finish(&state);

	while (true) {
		while (bftw_pop_dir(&state)) {
			if (bftw_opendir(&state) != 0) {
				goto done;
			}
			while (bftw_readdir(&state) > 0) {
				if (bftw_visit(&state, state.de->name) != 0) {
					goto done;
				}
			}
			if (bftw_closedir(&state) != 0) {
				goto done;
			}
		}

		if (!bftw_pop_file(&state)) {
			break;
		}
		if (bftw_visit(&state, NULL) != 0) {
			break;
		}
	}

done:
	return bftw_state_destroy(&state);
}

/**
 * Iterative deepening search state.
 */
struct bftw_ids_state {
	/** The wrapped callback. */
	bftw_callback *delegate;
	/** The wrapped callback arguments. */
	void *ptr;
	/** Which visit this search corresponds to. */
	enum bftw_visit visit;
	/** Whether to override the bftw_visit. */
	bool force_visit;
	/** The current minimum depth (inclusive). */
	size_t min_depth;
	/** The current maximum depth (exclusive). */
	size_t max_depth;
	/** The set of pruned paths. */
	struct trie pruned;
	/** An error code to report. */
	int error;
	/** Whether the bottom has been found. */
	bool bottom;
	/** Whether to quit the search. */
	bool quit;
};

/** Iterative deepening callback function. */
static enum bftw_action bftw_ids_callback(const struct BFTW *ftwbuf, void *ptr) {
	struct bftw_ids_state *state = ptr;

	if (state->force_visit) {
		struct BFTW *mutbuf = (struct BFTW *)ftwbuf;
		mutbuf->visit = state->visit;
	}

	if (ftwbuf->type == BFS_ERROR) {
		if (ftwbuf->depth + 1 >= state->min_depth) {
			return state->delegate(ftwbuf, state->ptr);
		} else {
			return BFTW_PRUNE;
		}
	}

	if (ftwbuf->depth < state->min_depth) {
		if (trie_find_str(&state->pruned, ftwbuf->path)) {
			return BFTW_PRUNE;
		} else {
			return BFTW_CONTINUE;
		}
	} else if (state->visit == BFTW_POST) {
		if (trie_find_str(&state->pruned, ftwbuf->path)) {
			return BFTW_PRUNE;
		}
	}

	enum bftw_action ret = BFTW_CONTINUE;
	if (ftwbuf->visit == state->visit) {
		ret = state->delegate(ftwbuf, state->ptr);
	}

	switch (ret) {
	case BFTW_CONTINUE:
		if (ftwbuf->type == BFS_DIR && ftwbuf->depth + 1 >= state->max_depth) {
			state->bottom = false;
			ret = BFTW_PRUNE;
		}
		break;
	case BFTW_PRUNE:
		if (ftwbuf->type == BFS_DIR) {
			if (!trie_insert_str(&state->pruned, ftwbuf->path)) {
				state->error = errno;
				state->quit = true;
				ret = BFTW_STOP;
			}
		}
		break;
	case BFTW_STOP:
		state->quit = true;
		break;
	}

	return ret;
}

/** Initialize iterative deepening state. */
static void bftw_ids_init(const struct bftw_args *args, struct bftw_ids_state *state, struct bftw_args *ids_args) {
	state->delegate = args->callback;
	state->ptr = args->ptr;
	state->visit = BFTW_PRE;
	state->force_visit = false;
	state->min_depth = 0;
	state->max_depth = 1;
	trie_init(&state->pruned);
	state->error = 0;
	state->bottom = false;
	state->quit = false;

	*ids_args = *args;
	ids_args->callback = bftw_ids_callback;
	ids_args->ptr = state;
	ids_args->flags &= ~BFTW_POST_ORDER;
}

/** Finish an iterative deepening search. */
static int bftw_ids_finish(struct bftw_ids_state *state) {
	int ret = 0;

	if (state->error) {
		ret = -1;
	} else {
		state->error = errno;
	}

	trie_destroy(&state->pruned);

	errno = state->error;
	return ret;
}

/**
 * Iterative deepening bftw() wrapper.
 */
static int bftw_ids(const struct bftw_args *args) {
	struct bftw_ids_state state;
	struct bftw_args ids_args;
	bftw_ids_init(args, &state, &ids_args);

	while (!state.quit && !state.bottom) {
		state.bottom = true;

		if (bftw_impl(&ids_args) != 0) {
			state.error = errno;
			state.quit = true;
		}

		++state.min_depth;
		++state.max_depth;
	}

	if (args->flags & BFTW_POST_ORDER) {
		state.visit = BFTW_POST;
		state.force_visit = true;

		while (!state.quit && state.min_depth > 0) {
			--state.max_depth;
			--state.min_depth;

			if (bftw_impl(&ids_args) != 0) {
				state.error = errno;
				state.quit = true;
			}
		}
	}

	return bftw_ids_finish(&state);
}

/**
 * Exponential deepening bftw() wrapper.
 */
static int bftw_eds(const struct bftw_args *args) {
	struct bftw_ids_state state;
	struct bftw_args ids_args;
	bftw_ids_init(args, &state, &ids_args);

	while (!state.quit && !state.bottom) {
		state.bottom = true;

		if (bftw_impl(&ids_args) != 0) {
			state.error = errno;
			state.quit = true;
		}

		state.min_depth = state.max_depth;
		state.max_depth *= 2;
	}

	if (!state.quit && (args->flags & BFTW_POST_ORDER)) {
		state.visit = BFTW_POST;
		state.min_depth = 0;
		ids_args.flags |= BFTW_POST_ORDER;

		if (bftw_impl(&ids_args) != 0) {
			state.error = errno;
		}
	}

	return bftw_ids_finish(&state);
}

int bftw(const struct bftw_args *args) {
	switch (args->strategy) {
	case BFTW_BFS:
	case BFTW_DFS:
		return bftw_impl(args);
	case BFTW_IDS:
		return bftw_ids(args);
	case BFTW_EDS:
		return bftw_eds(args);
	}

	errno = EINVAL;
	return -1;
}
