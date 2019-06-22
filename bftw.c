/****************************************************************************
 * bfs                                                                      *
 * Copyright (C) 2015-2019 Tavian Barnes <tavianator@tavianator.com>        *
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

/**
 * The bftw() implementation consists of the following components:
 *
 * - struct bftw_dir: A directory that has been encountered during the
 *   traversal.  They have reference-counted links to their parents in the
 *   directory tree.
 *
 * - struct bftw_cache: Holds bftw_dir's with open file descriptors, used for
 *   openat() to minimize the amount of path re-traversals that need to happen.
 *   Currently implemented as a priority queue based on depth and reference
 *   count.
 *
 * - struct bftw_queue: The queue of bftw_dir's left to explore.  Implemented as
 *   a simple circular buffer.
 *
 * - struct bftw_reader: A reader object that simplifies reading directories and
 *   reporting errors.
 *
 * - struct bftw_state: Represents the current state of the traversal, allowing
 *   various helper functions to take fewer parameters.
 */

#include "bftw.h"
#include "dstring.h"
#include "stat.h"
#include "trie.h"
#include "util.h"
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/**
 * A directory.
 */
struct bftw_dir {
	/** The parent directory, if any. */
	struct bftw_dir *parent;
	/** This directory's depth in the walk. */
	size_t depth;
	/** The root path this directory was found from. */
	const char *root;

	/** The next directory in the queue, if any. */
	struct bftw_dir *next;

	/** Reference count. */
	size_t refcount;
	/** Index in the bftw_cache priority queue. */
	size_t heap_index;

	/** An open file descriptor to this directory, or -1. */
	int fd;

	/** The device number, for cycle detection. */
	dev_t dev;
	/** The inode number, for cycle detection. */
	ino_t ino;

	/** The offset of this directory in the full path. */
	size_t nameoff;
	/** The length of the directory's name. */
	size_t namelen;
	/** The directory's name. */
	char name[];
};

/**
 * A cache of open directories.
 */
struct bftw_cache {
	/** A min-heap of open directories. */
	struct bftw_dir **heap;
	/** Current heap size. */
	size_t size;
	/** Maximum heap size. */
	size_t capacity;
};

/** Initialize a cache. */
static int bftw_cache_init(struct bftw_cache *cache, size_t capacity) {
	cache->heap = malloc(capacity*sizeof(*cache->heap));
	if (!cache->heap) {
		return -1;
	}

	cache->size = 0;
	cache->capacity = capacity;
	return 0;
}

/** Destroy a cache. */
static void bftw_cache_destroy(struct bftw_cache *cache) {
	assert(cache->size == 0);
	free(cache->heap);
}

/** Check if two heap entries are in heap order. */
static bool bftw_heap_check(const struct bftw_dir *parent, const struct bftw_dir *child) {
	if (parent->depth > child->depth) {
		return true;
	} else if (parent->depth < child->depth) {
		return false;
	} else {
		return parent->refcount <= child->refcount;
	}
}

/** Move a bftw_dir to a particular place in the heap. */
static void bftw_heap_move(struct bftw_cache *cache, struct bftw_dir *dir, size_t i) {
	cache->heap[i] = dir;
	dir->heap_index = i;
}

/** Bubble an entry up the heap. */
static void bftw_heap_bubble_up(struct bftw_cache *cache, struct bftw_dir *dir) {
	size_t i = dir->heap_index;

	while (i > 0) {
		size_t pi = (i - 1)/2;
		struct bftw_dir *parent = cache->heap[pi];
		if (bftw_heap_check(parent, dir)) {
			break;
		}

		bftw_heap_move(cache, parent, i);
		i = pi;
	}

	bftw_heap_move(cache, dir, i);
}

/** Bubble an entry down the heap. */
static void bftw_heap_bubble_down(struct bftw_cache *cache, struct bftw_dir *dir) {
	size_t i = dir->heap_index;

	while (true) {
		size_t ci = 2*i + 1;
		if (ci >= cache->size) {
			break;
		}

		struct bftw_dir *child = cache->heap[ci];

		size_t ri = ci + 1;
		if (ri < cache->size) {
			struct bftw_dir *right = cache->heap[ri];
			if (!bftw_heap_check(child, right)) {
				ci = ri;
				child = right;
			}
		}

		if (bftw_heap_check(dir, child)) {
			break;
		}

		bftw_heap_move(cache, child, i);
		i = ci;
	}

	bftw_heap_move(cache, dir, i);
}

/** Bubble an entry up or down the heap. */
static void bftw_heap_bubble(struct bftw_cache *cache, struct bftw_dir *dir) {
	size_t i = dir->heap_index;

	if (i > 0) {
		size_t pi = (i - 1)/2;
		struct bftw_dir *parent = cache->heap[pi];
		if (!bftw_heap_check(parent, dir)) {
			bftw_heap_bubble_up(cache, dir);
			return;
		}
	}

	bftw_heap_bubble_down(cache, dir);
}

/** Increment a bftw_dir's reference count. */
static void bftw_dir_incref(struct bftw_cache *cache, struct bftw_dir *dir) {
	++dir->refcount;

	if (dir->fd >= 0) {
		bftw_heap_bubble_down(cache, dir);
	}
}

/** Decrement a bftw_dir's reference count. */
static void bftw_dir_decref(struct bftw_cache *cache, struct bftw_dir *dir) {
	--dir->refcount;

	if (dir->fd >= 0) {
		bftw_heap_bubble_up(cache, dir);
	}
}

/** Add a bftw_dir to the cache. */
static void bftw_cache_add(struct bftw_cache *cache, struct bftw_dir *dir) {
	assert(cache->size < cache->capacity);
	assert(dir->fd >= 0);

	size_t size = cache->size++;
	dir->heap_index = size;
	bftw_heap_bubble_up(cache, dir);
}

/** Remove a bftw_dir from the cache. */
static void bftw_cache_remove(struct bftw_cache *cache, struct bftw_dir *dir) {
	assert(cache->size > 0);
	assert(dir->fd >= 0);

	size_t size = --cache->size;
	size_t i = dir->heap_index;
	if (i != size) {
		struct bftw_dir *end = cache->heap[size];
		end->heap_index = i;
		bftw_heap_bubble(cache, end);
	}
}

/** Close a bftw_dir. */
static void bftw_dir_close(struct bftw_cache *cache, struct bftw_dir *dir) {
	assert(dir->fd >= 0);

	bftw_cache_remove(cache, dir);

	close(dir->fd);
	dir->fd = -1;
}

/** Pop a directory from the cache. */
static void bftw_cache_pop(struct bftw_cache *cache) {
	assert(cache->size > 0);
	bftw_dir_close(cache, cache->heap[0]);
}

/**
 * Shrink the cache, to recover from EMFILE.
 *
 * @param cache
 *         The cache in question.
 * @param saved
 *         A bftw_dir that must be preserved.
 * @return
 *         0 if successfully shrunk, otherwise -1.
 */
static int bftw_cache_shrink(struct bftw_cache *cache, const struct bftw_dir *saved) {
	int ret = -1;
	struct bftw_dir *dir = NULL;

	if (cache->size >= 1) {
		dir = cache->heap[0];
		if (dir == saved && cache->size >= 2) {
			dir = cache->heap[1];
		}
	}

	if (dir && dir != saved) {
		bftw_dir_close(cache, dir);
		ret = 0;
	}

	cache->capacity = cache->size;
	return ret;
}

/** Compute the name offset of a child path. */
static size_t bftw_child_nameoff(const struct bftw_dir *parent) {
	size_t ret = parent->nameoff + parent->namelen;
	if (parent->name[parent->namelen - 1] != '/') {
		++ret;
	}
	return ret;
}

/** Create a new bftw_dir. */
static struct bftw_dir *bftw_dir_new(struct bftw_cache *cache, struct bftw_dir *parent, const char *name) {
	size_t namelen = strlen(name);
	size_t size = sizeof(struct bftw_dir) + namelen + 1;

	struct bftw_dir *dir = malloc(size);
	if (!dir) {
		return NULL;
	}

	dir->parent = parent;

	if (parent) {
		dir->depth = parent->depth + 1;
		dir->root = parent->root;
		dir->nameoff = bftw_child_nameoff(parent);
		bftw_dir_incref(cache, parent);
	} else {
		dir->depth = 0;
		dir->root = name;
		dir->nameoff = 0;
	}

	dir->next = NULL;

	dir->refcount = 1;
	dir->fd = -1;

	dir->dev = -1;
	dir->ino = -1;

	dir->namelen = namelen;
	memcpy(dir->name, name, namelen + 1);

	return dir;
}

/**
 * Compute the path to a bftw_dir.
 */
static int bftw_dir_path(const struct bftw_dir *dir, char **path) {
	size_t pathlen = dir->nameoff + dir->namelen;
	if (dstresize(path, pathlen) != 0) {
		return -1;
	}
	char *dest = *path;

	// Build the path backwards
	while (dir) {
		if (dir->nameoff > 0) {
			dest[dir->nameoff - 1] = '/';
		}
		memcpy(dest + dir->nameoff, dir->name, dir->namelen);
		dir = dir->parent;
	}

	return 0;
}

/**
 * Get the appropriate (fd, path) pair for the *at() family of functions.
 *
 * @param dir
 *         The directory being accessed.
 * @param[out] at_fd
 *         Will hold the appropriate file descriptor to use.
 * @param[in,out] at_path
 *         Will hold the appropriate path to use.
 * @return The closest open ancestor file.
 */
static struct bftw_dir *bftw_dir_base(struct bftw_dir *dir, int *at_fd, const char **at_path) {
	struct bftw_dir *base = dir;

	do {
		base = base->parent;
	} while (base && base->fd < 0);

	if (base) {
		*at_fd = base->fd;
		*at_path += bftw_child_nameoff(base);
	}

	return base;
}

/**
 * Open a bftw_dir relative to another one.
 *
 * @param cache
 *         The cache containing the dir.
 * @param dir
 *         The directory to open.
 * @param base
 *         The base directory for the relative path (may be NULL).
 * @param at_fd
 *         The base file descriptor, AT_FDCWD if base == NULL.
 * @param at_path
 *         The relative path to the dir.
 * @return
 *         The opened file descriptor, or negative on error.
 */
static int bftw_dir_openat(struct bftw_cache *cache, struct bftw_dir *dir, const struct bftw_dir *base, int at_fd, const char *at_path) {
	assert(dir->fd < 0);

	int flags = O_RDONLY | O_CLOEXEC | O_DIRECTORY;
	int fd = openat(at_fd, at_path, flags);

	if (fd < 0 && errno == EMFILE) {
		if (bftw_cache_shrink(cache, base) == 0) {
			fd = openat(base->fd, at_path, flags);
		}
	}

	if (fd >= 0) {
		if (cache->size == cache->capacity) {
			bftw_cache_pop(cache);
		}

		dir->fd = fd;
		bftw_cache_add(cache, dir);
	}

	return fd;
}

/**
 * Open a bftw_dir.
 *
 * @param cache
 *         The cache containing the directory.
 * @param dir
 *         The directory to open.
 * @param path
 *         The full path to the dir.
 * @return
 *         The opened file descriptor, or negative on error.
 */
static int bftw_dir_open(struct bftw_cache *cache, struct bftw_dir *dir, const char *path) {
	int at_fd = AT_FDCWD;
	const char *at_path = path;
	struct bftw_dir *base = bftw_dir_base(dir, &at_fd, &at_path);

	int fd = bftw_dir_openat(cache, dir, base, at_fd, at_path);
	if (fd >= 0 || errno != ENAMETOOLONG) {
		return fd;
	}

	// Handle ENAMETOOLONG by manually traversing the path component-by-component

	// -1 to include the root, which has depth == 0
	size_t offset = base ? base->depth : -1;
	size_t levels = dir->depth - offset;
	if (levels < 2) {
		return fd;
	}

	struct bftw_dir **parents = malloc(levels * sizeof(*parents));
	if (!parents) {
		return fd;
	}

	struct bftw_dir *parent = dir;
	for (size_t i = levels; i-- > 0;) {
		parents[i] = parent;
		parent = parent->parent;
	}

	for (size_t i = 0; i < levels; ++i) {
		fd = bftw_dir_openat(cache, parents[i], base, at_fd, parents[i]->name);
		if (fd < 0) {
			break;
		}

		base = parents[i];
		at_fd = fd;
	}

	free(parents);
	return fd;
}

/**
 * Open a DIR* for a bftw_dir.
 *
 * @param cache
 *         The cache containing the directory.
 * @param dir
 *         The directory to open.
 * @param path
 *         The full path to the directory.
 * @return
 *         The opened DIR *, or NULL on error.
 */
static DIR *bftw_dir_opendir(struct bftw_cache *cache, struct bftw_dir *dir, const char *path) {
	int fd = bftw_dir_open(cache, dir, path);
	if (fd < 0) {
		return NULL;
	}

	// Now we dup() the fd and pass it to fdopendir().  This way we can
	// close the DIR* as soon as we're done with it, reducing the memory
	// footprint significantly, while keeping the fd around for future
	// openat() calls.

	int dfd = dup_cloexec(fd);

	if (dfd < 0 && errno == EMFILE) {
		if (bftw_cache_shrink(cache, dir) == 0) {
			dfd = dup_cloexec(fd);
		}
	}

	if (dfd < 0) {
		return NULL;
	}

	DIR *ret = fdopendir(dfd);
	if (!ret) {
		int error = errno;
		close(dfd);
		errno = error;
	}

	return ret;
}

/** Free a bftw_dir. */
static void bftw_dir_free(struct bftw_cache *cache, struct bftw_dir *dir) {
	assert(dir->refcount == 0);

	if (dir->fd >= 0) {
		bftw_dir_close(cache, dir);
	}

	free(dir);
}

/**
 * A queue of bftw_dir's to examine.
 */
struct bftw_queue {
	/** The head of the queue. */
	struct bftw_dir *head;
	/** The tail of the queue. */
	struct bftw_dir *tail;
};

/** Initialize a bftw_queue. */
static void bftw_queue_init(struct bftw_queue *queue) {
	queue->head = NULL;
	queue->tail = NULL;
}

/** Add a directory to the tail of the bftw_queue. */
static void bftw_queue_push(struct bftw_queue *queue, struct bftw_dir *dir) {
	assert(dir->next == NULL);

	if (!queue->head) {
		queue->head = dir;
	}
	if (queue->tail) {
		queue->tail->next = dir;
	}
	queue->tail = dir;
}

/** Prepend a queue to the head of another one. */
static void bftw_queue_prepend(struct bftw_queue *head, struct bftw_queue *tail) {
	if (head->tail) {
		head->tail->next = tail->head;
	}
	if (head->head) {
		tail->head = head->head;
	}
	if (!tail->tail) {
		tail->tail = head->tail;
	}
	head->head = NULL;
	head->tail = NULL;
}

/** Pop the next directory from the head of the queue. */
static struct bftw_dir *bftw_queue_pop(struct bftw_queue *queue) {
	struct bftw_dir *dir = queue->head;
	queue->head = dir->next;
	if (queue->tail == dir) {
		queue->tail = NULL;
	}
	dir->next = NULL;
	return dir;
}

/**
 * A directory reader.
 */
struct bftw_reader {
	/** The directory object. */
	struct bftw_dir *dir;
	/** The open handle to the directory. */
	DIR *handle;
	/** The current directory entry. */
	struct dirent *de;
	/** Any error code that has occurred. */
	int error;
};

/** Initialize a reader. */
static void bftw_reader_init(struct bftw_reader *reader) {
	reader->dir = NULL;
	reader->handle = NULL;
	reader->de = NULL;
	reader->error = 0;
}

/** Open a directory for reading. */
static int bftw_reader_open(struct bftw_reader *reader, struct bftw_cache *cache, struct bftw_dir *dir, const char *path) {
	assert(!reader->dir);
	assert(!reader->handle);
	assert(!reader->de);

	reader->dir = dir;
	reader->error = 0;

	reader->handle = bftw_dir_opendir(cache, dir, path);
	if (!reader->handle) {
		reader->error = errno;
		return -1;
	}

	return 0;
}

/** Read a directory entry. */
static int bftw_reader_read(struct bftw_reader *reader) {
	if (!reader->handle) {
		return -1;
	}

	if (xreaddir(reader->handle, &reader->de) != 0) {
		reader->error = errno;
		return -1;
	} else if (reader->de) {
		return 1;
	} else {
		return 0;
	}
}

/** Close a directory. */
static int bftw_reader_close(struct bftw_reader *reader) {
	int ret = 0;
	if (reader->handle && closedir(reader->handle) != 0) {
		reader->error = errno;
		ret = -1;
	}

	reader->de = NULL;
	reader->handle = NULL;
	reader->dir = NULL;
	return ret;
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
	/** The queue of directories left to explore. */
	struct bftw_queue queue;
	/** An intermediate queue used for depth-first searches. */
	struct bftw_queue prequeue;

	/** The current path. */
	char *path;
	/** The reader for the current directory. */
	struct bftw_reader reader;

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

	state->error = 0;

	if (args->nopenfd < 2) {
		errno = EMFILE;
		goto err;
	}

	// Reserve 1 fd for the open DIR *
	if (bftw_cache_init(&state->cache, args->nopenfd - 1) != 0) {
		goto err;
	}

	bftw_queue_init(&state->queue);
	bftw_queue_init(&state->prequeue);

	state->path = dstralloc(0);
	if (!state->path) {
		goto err_cache;
	}

	bftw_reader_init(&state->reader);

	return 0;

err_cache:
	bftw_cache_destroy(&state->cache);
err:
	return -1;
}

enum bftw_typeflag bftw_mode_typeflag(mode_t mode) {
	switch (mode & S_IFMT) {
#ifdef S_IFBLK
	case S_IFBLK:
		return BFTW_BLK;
#endif
#ifdef S_IFCHR
	case S_IFCHR:
		return BFTW_CHR;
#endif
#ifdef S_IFDIR
	case S_IFDIR:
		return BFTW_DIR;
#endif
#ifdef S_IFDOOR
	case S_IFDOOR:
		return BFTW_DOOR;
#endif
#ifdef S_IFIFO
	case S_IFIFO:
		return BFTW_FIFO;
#endif
#ifdef S_IFLNK
	case S_IFLNK:
		return BFTW_LNK;
#endif
#ifdef S_IFPORT
	case S_IFPORT:
		return BFTW_PORT;
#endif
#ifdef S_IFREG
	case S_IFREG:
		return BFTW_REG;
#endif
#ifdef S_IFSOCK
	case S_IFSOCK:
		return BFTW_SOCK;
#endif
#ifdef S_IFWHT
	case S_IFWHT:
		return BFTW_WHT;
#endif

	default:
		return BFTW_UNKNOWN;
	}
}

static enum bftw_typeflag bftw_dirent_typeflag(const struct dirent *de) {
#if defined(_DIRENT_HAVE_D_TYPE) || defined(DT_UNKNOWN)
	switch (de->d_type) {
#ifdef DT_BLK
	case DT_BLK:
		return BFTW_BLK;
#endif
#ifdef DT_CHR
	case DT_CHR:
		return BFTW_CHR;
#endif
#ifdef DT_DIR
	case DT_DIR:
		return BFTW_DIR;
#endif
#ifdef DT_DOOR
	case DT_DOOR:
		return BFTW_DOOR;
#endif
#ifdef DT_FIFO
	case DT_FIFO:
		return BFTW_FIFO;
#endif
#ifdef DT_LNK
	case DT_LNK:
		return BFTW_LNK;
#endif
#ifdef DT_PORT
	case DT_PORT:
		return BFTW_PORT;
#endif
#ifdef DT_REG
	case DT_REG:
		return BFTW_REG;
#endif
#ifdef DT_SOCK
	case DT_SOCK:
		return BFTW_SOCK;
#endif
#ifdef DT_WHT
	case DT_WHT:
		return BFTW_WHT;
#endif
	}
#endif

	return BFTW_UNKNOWN;
}

/** Cached bfs_stat(). */
static const struct bfs_stat *bftw_stat_impl(struct BFTW *ftwbuf, struct bftw_stat *cache, enum bfs_stat_flag flags) {
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

const struct bfs_stat *bftw_stat(const struct BFTW *ftwbuf, enum bfs_stat_flag flags) {
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

enum bftw_typeflag bftw_typeflag(const struct BFTW *ftwbuf, enum bfs_stat_flag flags) {
	if (ftwbuf->stat_flags & BFS_STAT_NOFOLLOW) {
		if ((flags & BFS_STAT_NOFOLLOW) || ftwbuf->typeflag != BFTW_LNK) {
			return ftwbuf->typeflag;
		}
	} else if ((flags & (BFS_STAT_NOFOLLOW | BFS_STAT_TRYFOLLOW)) == BFS_STAT_TRYFOLLOW || ftwbuf->typeflag == BFTW_LNK) {
		return ftwbuf->typeflag;
	}

	const struct bfs_stat *statbuf = bftw_stat(ftwbuf, flags);
	if (statbuf) {
		return bftw_mode_typeflag(statbuf->mode);
	} else {
		return BFTW_ERROR;
	}
}

/**
 * Update the path for the current file.
 */
static int bftw_update_path(struct bftw_state *state, const struct bftw_dir *dir, const char *name) {
	size_t length = dir ? dir->nameoff + dir->namelen : 0;

	assert(dstrlen(state->path) >= length);
	dstresize(&state->path, length);

	if (name) {
		if (length > 0 && state->path[length - 1] != '/') {
			if (dstrapp(&state->path, '/') != 0) {
				return -1;
			}
		}
		if (dstrcat(&state->path, name) != 0) {
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
	if (ftwbuf->typeflag == BFTW_UNKNOWN) {
		return true;
	}

	if (ftwbuf->typeflag == BFTW_LNK && !(ftwbuf->stat_flags & BFS_STAT_NOFOLLOW)) {
		return true;
	}

	if (ftwbuf->typeflag == BFTW_DIR) {
		if (state->flags & (BFTW_DETECT_CYCLES | BFTW_XDEV)) {
			return true;
		}
#if __linux__
	} else if (state->mtab) {
		// Linux fills in d_type from the underlying inode, even when
		// the directory entry is a bind mount point.  In that case, we
		// need to stat() to get the correct type.  We don't need to
		// check for directories because they can only be mounted over
		// by other directories.
		if (bfs_maybe_mount(state->mtab, ftwbuf->path)) {
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
 * Initialize the buffers with data about the current path.
 */
static void bftw_init_ftwbuf(struct bftw_state *state, struct bftw_dir *dir, enum bftw_visit visit) {
	const struct bftw_reader *reader = &state->reader;
	const struct dirent *de = reader->de;

	struct BFTW *ftwbuf = &state->ftwbuf;
	ftwbuf->path = state->path;
	ftwbuf->root = dir ? dir->root : ftwbuf->path;
	ftwbuf->depth = 0;
	ftwbuf->visit = visit;
	ftwbuf->error = reader->error;
	ftwbuf->at_fd = AT_FDCWD;
	ftwbuf->at_path = ftwbuf->path;
	ftwbuf->stat_flags = BFS_STAT_NOFOLLOW;
	bftw_stat_init(&ftwbuf->lstat_cache);
	bftw_stat_init(&ftwbuf->stat_cache);

	if (dir) {
		ftwbuf->depth = dir->depth;

		if (de) {
			++ftwbuf->depth;
			ftwbuf->nameoff = bftw_child_nameoff(dir);

			ftwbuf->at_fd = dir->fd;
			ftwbuf->at_path += ftwbuf->nameoff;
		} else {
			ftwbuf->nameoff = dir->nameoff;
			bftw_dir_base(dir, &ftwbuf->at_fd, &ftwbuf->at_path);
		}
	}

	if (ftwbuf->depth == 0) {
		// Compute the name offset for root paths like "foo/bar"
		ftwbuf->nameoff = xbasename(ftwbuf->path) - ftwbuf->path;
	}

	if (ftwbuf->error != 0) {
		ftwbuf->typeflag = BFTW_ERROR;
		return;
	} else if (de) {
		ftwbuf->typeflag = bftw_dirent_typeflag(de);
	} else if (ftwbuf->depth > 0) {
		ftwbuf->typeflag = BFTW_DIR;
	} else {
		ftwbuf->typeflag = BFTW_UNKNOWN;
	}

	int follow_flags = BFTW_LOGICAL;
	if (ftwbuf->depth == 0) {
		follow_flags |= BFTW_COMFOLLOW;
	}
	bool follow = state->flags & follow_flags;
	if (follow) {
		ftwbuf->stat_flags = BFS_STAT_TRYFOLLOW;
	}

	const struct bfs_stat *statbuf = NULL;
	if (bftw_need_stat(state)) {
		statbuf = bftw_stat(ftwbuf, ftwbuf->stat_flags);
		if (statbuf) {
			ftwbuf->typeflag = bftw_mode_typeflag(statbuf->mode);
		} else {
			ftwbuf->typeflag = BFTW_ERROR;
			ftwbuf->error = errno;
			return;
		}
	}

	if (ftwbuf->typeflag == BFTW_DIR && (state->flags & BFTW_DETECT_CYCLES)) {
		for (const struct bftw_dir *parent = dir; parent; parent = parent->parent) {
			if (parent->depth == ftwbuf->depth) {
				continue;
			}
			if (parent->dev == statbuf->dev && parent->ino == statbuf->ino) {
				ftwbuf->typeflag = BFTW_ERROR;
				ftwbuf->error = ELOOP;
				return;
			}
		}
	}
}

/**
 * Visit a path, invoking the callback.
 */
static enum bftw_action bftw_visit(struct bftw_state *state, struct bftw_dir *dir, const char *name, enum bftw_visit visit) {
	if (bftw_update_path(state, dir, name) != 0) {
		state->error = errno;
		return BFTW_STOP;
	}

	const struct BFTW *ftwbuf = &state->ftwbuf;
	bftw_init_ftwbuf(state, dir, visit);

	// Never give the callback BFTW_ERROR unless BFTW_RECOVER is specified
	if (ftwbuf->typeflag == BFTW_ERROR && !(state->flags & BFTW_RECOVER)) {
		state->error = ftwbuf->error;
		return BFTW_STOP;
	}

	enum bftw_action ret = state->callback(ftwbuf, state->ptr);
	switch (ret) {
	case BFTW_CONTINUE:
		break;
	case BFTW_SKIP_SIBLINGS:
	case BFTW_SKIP_SUBTREE:
	case BFTW_STOP:
		return ret;
	default:
		state->error = EINVAL;
		return BFTW_STOP;
	}

	if (visit != BFTW_PRE || ftwbuf->typeflag != BFTW_DIR) {
		return BFTW_SKIP_SUBTREE;
	}

	if ((state->flags & BFTW_XDEV) && dir) {
		const struct bfs_stat *statbuf = bftw_stat(ftwbuf, ftwbuf->stat_flags);
		if (statbuf && statbuf->dev != dir->dev) {
			return BFTW_SKIP_SUBTREE;
		}
	}

	return BFTW_CONTINUE;
}

/**
 * Push a new directory onto the queue.
 */
static int bftw_push(struct bftw_state *state, const char *name) {
	struct bftw_dir *parent = state->reader.dir;
	struct bftw_dir *dir = bftw_dir_new(&state->cache, parent, name);
	if (!dir) {
		state->error = errno;
		return -1;
	}

	const struct BFTW *ftwbuf = &state->ftwbuf;
	const struct bfs_stat *statbuf = ftwbuf->stat_cache.buf;
	if (!statbuf || (ftwbuf->stat_flags & BFS_STAT_NOFOLLOW)) {
		statbuf = ftwbuf->lstat_cache.buf;
	}
	if (statbuf) {
		dir->dev = statbuf->dev;
		dir->ino = statbuf->ino;
	}

	if (state->strategy == BFTW_DFS) {
		bftw_queue_push(&state->prequeue, dir);
	} else {
		bftw_queue_push(&state->queue, dir);
	}
	return 0;
}

/**
 * Pop a directory from the queue and start reading it.
 */
static struct bftw_reader *bftw_pop(struct bftw_state *state) {
	if (state->strategy == BFTW_DFS) {
		bftw_queue_prepend(&state->prequeue, &state->queue);
	}

	if (!state->queue.head) {
		return NULL;
	}

	struct bftw_reader *reader = &state->reader;
	struct bftw_dir *dir = state->queue.head;
	if (bftw_dir_path(dir, &state->path) != 0) {
		state->error = errno;
		return NULL;
	}

	bftw_queue_pop(&state->queue);
	bftw_reader_open(reader, &state->cache, dir, state->path);
	return reader;
}

/**
 * Finalize and free a directory we're done with.
 */
static enum bftw_action bftw_release_dir(struct bftw_state *state, struct bftw_dir *dir, bool do_visit) {
	enum bftw_action ret = BFTW_CONTINUE;

	if (!(state->flags & BFTW_DEPTH)) {
		do_visit = false;
	}

	while (dir) {
		bftw_dir_decref(&state->cache, dir);
		if (dir->refcount > 0) {
			break;
		}

		if (do_visit) {
			if (bftw_visit(state, dir, NULL, BFTW_POST) == BFTW_STOP) {
				ret = BFTW_STOP;
				do_visit = false;
			}
		}

		struct bftw_dir *parent = dir->parent;
		bftw_dir_free(&state->cache, dir);
		dir = parent;
	}

	return ret;
}

/**
 * Close and release a reader.
 */
static enum bftw_action bftw_release_reader(struct bftw_state *state, bool do_visit) {
	enum bftw_action ret = BFTW_CONTINUE;

	struct bftw_reader *reader = &state->reader;
	struct bftw_dir *dir = reader->dir;
	bftw_reader_close(reader);

	if (reader->error != 0) {
		if (do_visit) {
			if (bftw_visit(state, dir, NULL, BFTW_PRE) == BFTW_STOP) {
				ret = BFTW_STOP;
				do_visit = false;
			}
		} else {
			state->error = reader->error;
		}
		reader->error = 0;
	}

	if (bftw_release_dir(state, dir, do_visit) == BFTW_STOP) {
		ret = BFTW_STOP;
	}

	return ret;
}

/**
 * Drain all the entries from a bftw_queue.
 */
static void bftw_drain_queue(struct bftw_state *state, struct bftw_queue *queue) {
	while (queue->head) {
		struct bftw_dir *dir = bftw_queue_pop(queue);
		bftw_release_dir(state, dir, false);
	}
}

/**
 * Dispose of the bftw() state.
 *
 * @return
 *         The bftw() return value.
 */
static int bftw_state_destroy(struct bftw_state *state) {
	bftw_release_reader(state, false);
	dstrfree(state->path);

	bftw_drain_queue(state, &state->prequeue);
	bftw_drain_queue(state, &state->queue);

	bftw_cache_destroy(&state->cache);

	errno = state->error;
	return state->error ? -1 : 0;
}

/**
 * bftw() implementation for breadth- and depth-first search.
 */
static int bftw_impl(const struct bftw_args *args) {
	struct bftw_state state;
	if (bftw_state_init(&state, args) != 0) {
		return -1;
	}

	for (size_t i = 0; i < args->npaths; ++i) {
		const char *path = args->paths[i];

		switch (bftw_visit(&state, NULL, path, BFTW_PRE)) {
		case BFTW_CONTINUE:
			break;
		case BFTW_SKIP_SIBLINGS:
			goto start;
		case BFTW_SKIP_SUBTREE:
			continue;
		case BFTW_STOP:
			goto done;
		}

		if (bftw_push(&state, path) != 0) {
			goto done;
		}
	}

start:
	while (true) {
		struct bftw_reader *reader = bftw_pop(&state);
		if (!reader) {
			goto done;
		}

		while (bftw_reader_read(reader) > 0) {
			const char *name = reader->de->d_name;

			switch (bftw_visit(&state, reader->dir, name, BFTW_PRE)) {
			case BFTW_CONTINUE:
				break;
			case BFTW_SKIP_SIBLINGS:
				goto next;
			case BFTW_SKIP_SUBTREE:
				continue;
			case BFTW_STOP:
				goto done;
			}

			if (bftw_push(&state, name) != 0) {
				goto done;
			}
		}

	next:
		if (bftw_release_reader(&state, true) == BFTW_STOP) {
			goto done;
		}
	}

done:
	return bftw_state_destroy(&state);
}

/**
 * Depth-first search state.
 */
struct bftw_dfs_state {
	/** The wrapped callback. */
	bftw_callback *delegate;
	/** The wrapped callback arguments. */
	void *ptr;
	/** Whether to quit the search. */
	bool quit;
};

/** Depth-first callback function. */
static enum bftw_action bftw_dfs_callback(const struct BFTW *ftwbuf, void *ptr) {
	struct bftw_dfs_state *state = ptr;
	enum bftw_action ret = state->delegate(ftwbuf, state->ptr);
	if (ret == BFTW_STOP || (ret == BFTW_SKIP_SIBLINGS && ftwbuf->depth == 0)) {
		state->quit = true;
	}
	return ret;
}

/**
 * Depth-first bftw() wrapper.
 */
static int bftw_dfs(const struct bftw_args *args) {
	// bftw_impl() will process all the roots first, but we don't want that
	// for depth-first searches

	struct bftw_dfs_state state = {
		.delegate = args->callback,
		.ptr = args->ptr,
		.quit = false,
	};

	struct bftw_args dfs_args = *args;
	dfs_args.npaths = 1;
	dfs_args.callback = bftw_dfs_callback;
	dfs_args.ptr = &state;

	int ret = 0;
	for (size_t i = 0; i < args->npaths && ret == 0 && !state.quit; ++i) {
		ret = bftw_impl(&dfs_args);
		++dfs_args.paths;
	}
	return ret;
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
	/** The current target depth. */
	size_t depth;
	/** The set of pruned paths. */
	struct trie *pruned;
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

	struct BFTW *mutbuf = (struct BFTW *)ftwbuf;
	mutbuf->visit = state->visit;

	if (ftwbuf->typeflag == BFTW_ERROR) {
		if (state->depth - ftwbuf->depth <= 1) {
			return state->delegate(ftwbuf, state->ptr);
		} else {
			return BFTW_SKIP_SUBTREE;
		}
	}

	if (ftwbuf->depth < state->depth) {
		if (trie_find_str(state->pruned, ftwbuf->path)) {
			return BFTW_SKIP_SUBTREE;
		} else {
			return BFTW_CONTINUE;
		}
	} else if (state->visit == BFTW_POST) {
		if (trie_find_str(state->pruned, ftwbuf->path)) {
			return BFTW_SKIP_SUBTREE;
		}
	}

	state->bottom = false;

	enum bftw_action ret = state->delegate(ftwbuf, state->ptr);

	switch (ret) {
	case BFTW_CONTINUE:
		ret = BFTW_SKIP_SUBTREE;
		break;
	case BFTW_SKIP_SIBLINGS:
		// Can't be easily supported in this mode
		state->error = EINVAL;
		state->quit = true;
		ret = BFTW_STOP;
		break;
	case BFTW_SKIP_SUBTREE:
		if (ftwbuf->typeflag == BFTW_DIR) {
			if (!trie_insert_str(state->pruned, ftwbuf->path)) {
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

/**
 * Iterative deepening bftw() wrapper.
 */
static int bftw_ids(const struct bftw_args *args) {
	struct trie pruned;
	trie_init(&pruned);

	struct bftw_ids_state state = {
		.delegate = args->callback,
		.ptr = args->ptr,
		.visit = BFTW_PRE,
		.depth = 0,
		.pruned = &pruned,
		.bottom = false,
	};

	struct bftw_args ids_args = *args;
	ids_args.callback = bftw_ids_callback;
	ids_args.ptr = &state;
	ids_args.flags &= ~BFTW_DEPTH;
	ids_args.strategy = BFTW_DFS;

	int ret = 0;

	while (ret == 0 && !state.quit && !state.bottom) {
		state.bottom = true;
		ret = bftw_impl(&ids_args);
		++state.depth;
	}

	if (args->flags & BFTW_DEPTH) {
		state.visit = BFTW_POST;

		while (ret == 0 && !state.quit && state.depth > 0) {
			--state.depth;
			ret = bftw_impl(&ids_args);
		}
	}

	if (state.error) {
		ret = -1;
	} else {
		state.error = errno;
	}
	trie_destroy(&pruned);
	errno = state.error;
	return ret;
}

int bftw(const struct bftw_args *args) {
	switch (args->strategy) {
	case BFTW_BFS:
		return bftw_impl(args);
	case BFTW_DFS:
		return bftw_dfs(args);
	case BFTW_IDS:
		return bftw_ids(args);
	}

	errno = EINVAL;
	return -1;
}
