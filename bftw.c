/****************************************************************************
 * bfs                                                                      *
 * Copyright (C) 2015-2021 Tavian Barnes <tavianator@tavianator.com>        *
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
 * - struct bftw_file: A file that has been encountered during the traversal.
 *   They have reference-counted links to their parents in the directory tree.
 *
 * - struct bftw_cache: Holds bftw_file's with open file descriptors, used for
 *   openat() to minimize the amount of path re-traversals that need to happen.
 *   Currently implemented as a priority queue based on depth and reference
 *   count.
 *
 * - struct bftw_queue: The queue of bftw_file's left to explore.  Implemented
 *   as a simple circular buffer.
 *
 * - struct bftw_state: Represents the current state of the traversal, allowing
 *   various helper functions to take fewer parameters.
 */

#include "bftw.h"
#include "dir.h"
#include "dstring.h"
#include "mtab.h"
#include "stat.h"
#include "trie.h"
#include "util.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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

	/** This file's depth in the walk. */
	size_t depth;
	/** Reference count. */
	size_t refcount;
	/** Index in the bftw_cache priority queue. */
	size_t heap_index;

	/** An open descriptor to this file, or -1. */
	int fd;

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
 * A cache of open directories.
 */
struct bftw_cache {
	/** A min-heap of open directories. */
	struct bftw_file **heap;
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
static bool bftw_heap_check(const struct bftw_file *parent, const struct bftw_file *child) {
	if (parent->depth > child->depth) {
		return true;
	} else if (parent->depth < child->depth) {
		return false;
	} else {
		return parent->refcount <= child->refcount;
	}
}

/** Move a bftw_file to a particular place in the heap. */
static void bftw_heap_move(struct bftw_cache *cache, struct bftw_file *file, size_t i) {
	cache->heap[i] = file;
	file->heap_index = i;
}

/** Bubble an entry up the heap. */
static void bftw_heap_bubble_up(struct bftw_cache *cache, struct bftw_file *file) {
	size_t i = file->heap_index;

	while (i > 0) {
		size_t pi = (i - 1)/2;
		struct bftw_file *parent = cache->heap[pi];
		if (bftw_heap_check(parent, file)) {
			break;
		}

		bftw_heap_move(cache, parent, i);
		i = pi;
	}

	bftw_heap_move(cache, file, i);
}

/** Bubble an entry down the heap. */
static void bftw_heap_bubble_down(struct bftw_cache *cache, struct bftw_file *file) {
	size_t i = file->heap_index;

	while (true) {
		size_t ci = 2*i + 1;
		if (ci >= cache->size) {
			break;
		}

		struct bftw_file *child = cache->heap[ci];

		size_t ri = ci + 1;
		if (ri < cache->size) {
			struct bftw_file *right = cache->heap[ri];
			if (!bftw_heap_check(child, right)) {
				ci = ri;
				child = right;
			}
		}

		if (bftw_heap_check(file, child)) {
			break;
		}

		bftw_heap_move(cache, child, i);
		i = ci;
	}

	bftw_heap_move(cache, file, i);
}

/** Bubble an entry up or down the heap. */
static void bftw_heap_bubble(struct bftw_cache *cache, struct bftw_file *file) {
	size_t i = file->heap_index;

	if (i > 0) {
		size_t pi = (i - 1)/2;
		struct bftw_file *parent = cache->heap[pi];
		if (!bftw_heap_check(parent, file)) {
			bftw_heap_bubble_up(cache, file);
			return;
		}
	}

	bftw_heap_bubble_down(cache, file);
}

/** Increment a bftw_file's reference count. */
static size_t bftw_file_incref(struct bftw_cache *cache, struct bftw_file *file) {
	size_t ret = ++file->refcount;
	if (file->fd >= 0) {
		bftw_heap_bubble_down(cache, file);
	}
	return ret;
}

/** Decrement a bftw_file's reference count. */
static size_t bftw_file_decref(struct bftw_cache *cache, struct bftw_file *file) {
	size_t ret = --file->refcount;
	if (file->fd >= 0) {
		bftw_heap_bubble_up(cache, file);
	}
	return ret;
}

/** Add a bftw_file to the cache. */
static void bftw_cache_add(struct bftw_cache *cache, struct bftw_file *file) {
	assert(cache->size < cache->capacity);
	assert(file->fd >= 0);

	size_t size = cache->size++;
	file->heap_index = size;
	bftw_heap_bubble_up(cache, file);
}

/** Remove a bftw_file from the cache. */
static void bftw_cache_remove(struct bftw_cache *cache, struct bftw_file *file) {
	assert(cache->size > 0);

	size_t size = --cache->size;
	size_t i = file->heap_index;
	if (i != size) {
		struct bftw_file *end = cache->heap[size];
		end->heap_index = i;
		bftw_heap_bubble(cache, end);
	}
}

/** Close a bftw_file. */
static void bftw_file_close(struct bftw_cache *cache, struct bftw_file *file) {
	assert(file->fd >= 0);

	bftw_cache_remove(cache, file);

	close(file->fd);
	file->fd = -1;
}

/** Pop a directory from the cache. */
static void bftw_cache_pop(struct bftw_cache *cache) {
	assert(cache->size > 0);
	bftw_file_close(cache, cache->heap[0]);
}

/**
 * Shrink the cache, to recover from EMFILE.
 *
 * @param cache
 *         The cache in question.
 * @param saved
 *         A bftw_file that must be preserved.
 * @return
 *         0 if successfully shrunk, otherwise -1.
 */
static int bftw_cache_shrink(struct bftw_cache *cache, const struct bftw_file *saved) {
	int ret = -1;
	struct bftw_file *file = NULL;

	if (cache->size >= 1) {
		file = cache->heap[0];
		if (file == saved && cache->size >= 2) {
			file = cache->heap[1];
		}
	}

	if (file && file != saved) {
		bftw_file_close(cache, file);
		ret = 0;
	}

	cache->capacity = cache->size;
	return ret;
}

/** Compute the name offset of a child path. */
static size_t bftw_child_nameoff(const struct bftw_file *parent) {
	size_t ret = parent->nameoff + parent->namelen;
	if (parent->name[parent->namelen - 1] != '/') {
		++ret;
	}
	return ret;
}

/** Create a new bftw_file. */
static struct bftw_file *bftw_file_new(struct bftw_cache *cache, struct bftw_file *parent, const char *name) {
	size_t namelen = strlen(name);
	size_t size = BFS_FLEX_SIZEOF(struct bftw_file, name, namelen + 1);

	struct bftw_file *file = malloc(size);
	if (!file) {
		return NULL;
	}

	file->parent = parent;

	if (parent) {
		file->root = parent->root;
		file->depth = parent->depth + 1;
		file->nameoff = bftw_child_nameoff(parent);
		bftw_file_incref(cache, parent);
	} else {
		file->root = file;
		file->depth = 0;
		file->nameoff = 0;
	}

	file->next = NULL;

	file->refcount = 1;
	file->fd = -1;

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
static int bftw_file_openat(struct bftw_cache *cache, struct bftw_file *file, const struct bftw_file *base, const char *at_path) {
	assert(file->fd < 0);

	int at_fd = AT_FDCWD;
	if (base) {
		at_fd = base->fd;
		assert(at_fd >= 0);
	}

	int flags = O_RDONLY | O_CLOEXEC | O_DIRECTORY;
	int fd = openat(at_fd, at_path, flags);

	if (fd < 0 && errno == EMFILE) {
		if (bftw_cache_shrink(cache, base) == 0) {
			fd = openat(at_fd, at_path, flags);
		}
	}

	if (fd >= 0) {
		if (cache->size == cache->capacity) {
			bftw_cache_pop(cache);
		}

		file->fd = fd;
		bftw_cache_add(cache, file);
	}

	return fd;
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

	// Use the ->next linked list to temporarily hold the reversed parent
	// chain between base and file
	struct bftw_file *cur;
	for (cur = file; cur->parent != base; cur = cur->parent) {
		cur->parent->next = cur;
	}

	// Open the files in the chain one by one
	for (base = cur; base; base = base->next) {
		fd = bftw_file_openat(cache, base, base->parent, base->name);
		if (fd < 0 || base == file) {
			break;
		}
	}

	// Clear out the linked list
	for (struct bftw_file *next = cur->next; cur != file; cur = next, next = next->next) {
		cur->next = NULL;
	}

	return fd;
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

	return bfs_opendir(fd, NULL);
}

/** Free a bftw_file. */
static void bftw_file_free(struct bftw_cache *cache, struct bftw_file *file) {
	assert(file->refcount == 0);

	if (file->fd >= 0) {
		bftw_file_close(cache, file);
	}

	free(file);
}

/**
 * A queue of bftw_file's to examine.
 */
struct bftw_queue {
	/** The head of the queue. */
	struct bftw_file *head;
	/** The insertion target. */
	struct bftw_file **target;
};

/** Initialize a bftw_queue. */
static void bftw_queue_init(struct bftw_queue *queue) {
	queue->head = NULL;
	queue->target = &queue->head;
}

/** Add a file to a bftw_queue. */
static void bftw_queue_push(struct bftw_queue *queue, struct bftw_file *file) {
	assert(file->next == NULL);

	file->next = *queue->target;
	*queue->target = file;
	queue->target = &file->next;
}

/** Pop the next file from the head of the queue. */
static struct bftw_file *bftw_queue_pop(struct bftw_queue *queue) {
	struct bftw_file *file = queue->head;
	queue->head = file->next;
	file->next = NULL;
	if (queue->target == &file->next) {
		queue->target = &queue->head;
	}
	return file;
}

/** The split phase of mergesort. */
static struct bftw_file **bftw_sort_split(struct bftw_file **head, struct bftw_file **tail) {
	struct bftw_file **tortoise = head, **hare = head;

	while (*hare != *tail) {
		tortoise = &(*tortoise)->next;
		hare = &(*hare)->next;
		if (*hare != *tail) {
			hare = &(*hare)->next;
		}
	}

	return tortoise;
}

/** The merge phase of mergesort. */
static struct bftw_file **bftw_sort_merge(struct bftw_file **head, struct bftw_file **mid, struct bftw_file **tail) {
	struct bftw_file *left = *head, *right = *mid, *end = *tail;
	*mid = NULL;
	*tail = NULL;

	while (left || right) {
		struct bftw_file *next;
		if (left && (!right || strcoll(left->name, right->name) <= 0)) {
			next = left;
			left = left->next;
		} else {
			next = right;
			right = right->next;
		}

		*head = next;
		head = &next->next;
	}

	*head = end;
	return head;
}

/**
 * Sort a (sub-)list of files.
 *
 * @param head
 *         The head of the (sub-)list to sort.
 * @param tail
 *         The tail of the (sub-)list to sort.
 * @return
 *         The new tail of the (sub-)list.
 */
static struct bftw_file **bftw_sort_files(struct bftw_file **head, struct bftw_file **tail) {
	struct bftw_file **mid = bftw_sort_split(head, tail);
	if (*mid == *head || *mid == *tail) {
		return tail;
	}

	mid = bftw_sort_files(head, mid);
	tail = bftw_sort_files(mid, tail);

	return bftw_sort_merge(head, mid, tail);
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
	/** The start of the current batch of files. */
	struct bftw_file **batch;

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

	state->error = 0;

	if (args->nopenfd < 2) {
		errno = EMFILE;
		goto err;
	}

	// Reserve 1 fd for the open bfs_dir
	if (bftw_cache_init(&state->cache, args->nopenfd - 1) != 0) {
		goto err;
	}

	bftw_queue_init(&state->queue);
	state->batch = NULL;

	state->path = dstralloc(0);
	if (!state->path) {
		goto err_cache;
	}

	state->file = NULL;
	state->previous = NULL;

	state->dir = NULL;
	state->de = NULL;
	state->direrror = 0;

	return 0;

err_cache:
	bftw_cache_destroy(&state->cache);
err:
	return -1;
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
	if (ftwbuf->stat_flags & BFS_STAT_NOFOLLOW) {
		if ((flags & BFS_STAT_NOFOLLOW) || ftwbuf->type != BFS_LNK) {
			return ftwbuf->type;
		}
	} else if ((flags & (BFS_STAT_NOFOLLOW | BFS_STAT_TRYFOLLOW)) == BFS_STAT_TRYFOLLOW || ftwbuf->type == BFS_LNK) {
		return ftwbuf->type;
	}

	const struct bfs_stat *statbuf = bftw_stat(ftwbuf, flags);
	if (statbuf) {
		return bfs_mode_to_type(statbuf->mode);
	} else {
		return BFS_ERROR;
	}
}

/**
 * Update the path for the current file.
 */
static int bftw_update_path(struct bftw_state *state, const char *name) {
	const struct bftw_file *file = state->file;
	size_t length = file ? file->nameoff + file->namelen : 0;

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
		ftwbuf->nameoff = xbasename(ftwbuf->path) - ftwbuf->path;
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

/** Fill file identity information from an ftwbuf. */
static void bftw_fill_id(struct bftw_file *file, const struct BFTW *ftwbuf) {
	const struct bfs_stat *statbuf = ftwbuf->stat_cache.buf;
	if (!statbuf || (ftwbuf->stat_flags & BFS_STAT_NOFOLLOW)) {
		statbuf = ftwbuf->lstat_cache.buf;
	}
	if (statbuf) {
		file->dev = statbuf->dev;
		file->ino = statbuf->ino;
	}
}

/**
 * Visit a path, invoking the callback.
 */
static enum bftw_action bftw_visit(struct bftw_state *state, const char *name, enum bftw_visit visit) {
	if (bftw_update_path(state, name) != 0) {
		state->error = errno;
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
		break;
	case BFTW_PRUNE:
	case BFTW_STOP:
		goto done;
	default:
		state->error = EINVAL;
		return BFTW_STOP;
	}

	if (visit != BFTW_PRE || ftwbuf->type != BFS_DIR) {
		ret = BFTW_PRUNE;
		goto done;
	}

	if ((state->flags & BFTW_PRUNE_MOUNTS) && bftw_is_mount(state, name)) {
		ret = BFTW_PRUNE;
		goto done;
	}

done:
	if (state->file && !name) {
		bftw_fill_id(state->file, ftwbuf);
	}

	return ret;
}

/**
 * Push a new file onto the queue.
 */
static int bftw_push(struct bftw_state *state, const char *name, bool fill_id) {
	struct bftw_file *parent = state->file;
	struct bftw_file *file = bftw_file_new(&state->cache, parent, name);
	if (!file) {
		state->error = errno;
		return -1;
	}

	if (state->de) {
		file->type = state->de->type;
	}

	if (fill_id) {
		bftw_fill_id(file, &state->ftwbuf);
	}

	bftw_queue_push(&state->queue, file);

	return 0;
}

/**
 * Build the path to the current file.
 */
static int bftw_build_path(struct bftw_state *state) {
	const struct bftw_file *file = state->file;

	size_t pathlen = file->nameoff + file->namelen;
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
	return 0;
}

/**
 * Pop the next file from the queue.
 */
static int bftw_pop(struct bftw_state *state) {
	if (!state->queue.head) {
		return 0;
	}

	state->file = bftw_queue_pop(&state->queue);

	if (bftw_build_path(state) != 0) {
		return -1;
	}

	return 1;
}

/**
 * Open the current directory.
 */
static void bftw_opendir(struct bftw_state *state) {
	assert(!state->dir);
	assert(!state->de);

	state->direrror = 0;

	state->dir = bftw_file_opendir(&state->cache, state->file, state->path);
	if (!state->dir) {
		state->direrror = errno;
	}
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
	/** Visit the file itself. */
	BFTW_VISIT_FILE = 1 << 0,
	/** Visit the file's ancestors. */
	BFTW_VISIT_PARENTS = 1 << 1,
	/** Visit both the file and its ancestors. */
	BFTW_VISIT_ALL = BFTW_VISIT_FILE | BFTW_VISIT_PARENTS,
};

/**
 * Close the current directory.
 */
static enum bftw_action bftw_closedir(struct bftw_state *state, enum bftw_gc_flags flags) {
	struct bftw_file *file = state->file;
	enum bftw_action ret = BFTW_CONTINUE;

	if (state->dir) {
		assert(file->fd >= 0);

		if (file->refcount > 1) {
			// Keep the fd around if any subdirectories exist
			file->fd = bfs_freedir(state->dir);
		} else {
			bfs_closedir(state->dir);
			file->fd = -1;
		}

		if (file->fd < 0) {
			bftw_cache_remove(&state->cache, file);
		}
	}

	state->de = NULL;
	state->dir = NULL;

	if (state->direrror != 0) {
		if (flags & BFTW_VISIT_FILE) {
			ret = bftw_visit(state, NULL, BFTW_PRE);
		} else {
			state->error = state->direrror;
		}
		state->direrror = 0;
	}

	return ret;
}

/**
 * Finalize and free a file we're done with.
 */
static enum bftw_action bftw_gc_file(struct bftw_state *state, enum bftw_gc_flags flags) {
	enum bftw_action ret = BFTW_CONTINUE;

	if (!(state->flags & BFTW_POST_ORDER)) {
		flags = 0;
	}
	bool visit = flags & BFTW_VISIT_FILE;

	while (state->file) {
		if (bftw_file_decref(&state->cache, state->file) > 0) {
			state->file = NULL;
			break;
		}

		if (visit && bftw_visit(state, NULL, BFTW_POST) == BFTW_STOP) {
			ret = BFTW_STOP;
			flags &= ~BFTW_VISIT_PARENTS;
		}
		visit = flags & BFTW_VISIT_PARENTS;

		struct bftw_file *parent = state->file->parent;
		if (state->previous == state->file) {
			state->previous = parent;
		}
		bftw_file_free(&state->cache, state->file);
		state->file = parent;
	}

	return ret;
}

/**
 * Drain all the entries from a bftw_queue.
 */
static void bftw_drain_queue(struct bftw_state *state, struct bftw_queue *queue) {
	while (queue->head) {
		state->file = bftw_queue_pop(queue);
		bftw_gc_file(state, BFTW_VISIT_NONE);
	}
}

/**
 * Dispose of the bftw() state.
 *
 * @return
 *         The bftw() return value.
 */
static int bftw_state_destroy(struct bftw_state *state) {
	dstrfree(state->path);

	bftw_closedir(state, BFTW_VISIT_NONE);

	bftw_gc_file(state, BFTW_VISIT_NONE);
	bftw_drain_queue(state, &state->queue);

	bftw_cache_destroy(&state->cache);

	errno = state->error;
	return state->error ? -1 : 0;
}

/** Start a batch of files. */
static void bftw_batch_start(struct bftw_state *state) {
	if (state->strategy == BFTW_DFS) {
		state->queue.target = &state->queue.head;
	}
	state->batch = state->queue.target;
}

/** Finish adding a batch of files. */
static void bftw_batch_finish(struct bftw_state *state) {
	if (state->flags & BFTW_SORT) {
		state->queue.target = bftw_sort_files(state->batch, state->queue.target);
	}
}

/**
 * Streaming mode: visit files as they are encountered.
 */
static int bftw_stream(const struct bftw_args *args) {
	struct bftw_state state;
	if (bftw_state_init(&state, args) != 0) {
		return -1;
	}

	assert(!(state.flags & BFTW_SORT));

	bftw_batch_start(&state);
	for (size_t i = 0; i < args->npaths; ++i) {
		const char *path = args->paths[i];

		switch (bftw_visit(&state, path, BFTW_PRE)) {
		case BFTW_CONTINUE:
			break;
		case BFTW_PRUNE:
			continue;
		case BFTW_STOP:
			goto done;
		}

		if (bftw_push(&state, path, true) != 0) {
			goto done;
		}
	}
	bftw_batch_finish(&state);

	while (bftw_pop(&state) > 0) {
		bftw_opendir(&state);

		bftw_batch_start(&state);
		while (bftw_readdir(&state) > 0) {
			const char *name = state.de->name;

			switch (bftw_visit(&state, name, BFTW_PRE)) {
			case BFTW_CONTINUE:
				break;
			case BFTW_PRUNE:
				continue;
			case BFTW_STOP:
				goto done;
			}

			if (bftw_push(&state, name, true) != 0) {
				goto done;
			}
		}
		bftw_batch_finish(&state);

		if (bftw_closedir(&state, BFTW_VISIT_ALL) == BFTW_STOP) {
			goto done;
		}
		if (bftw_gc_file(&state, BFTW_VISIT_ALL) == BFTW_STOP) {
			goto done;
		}
	}

done:
	return bftw_state_destroy(&state);
}

/**
 * Batching mode: queue up all children before visiting them.
 */
static int bftw_batch(const struct bftw_args *args) {
	struct bftw_state state;
	if (bftw_state_init(&state, args) != 0) {
		return -1;
	}

	bftw_batch_start(&state);
	for (size_t i = 0; i < args->npaths; ++i) {
		if (bftw_push(&state, args->paths[i], false) != 0) {
			goto done;
		}
	}
	bftw_batch_finish(&state);

	while (bftw_pop(&state) > 0) {
		enum bftw_gc_flags gcflags = BFTW_VISIT_ALL;

		switch (bftw_visit(&state, NULL, BFTW_PRE)) {
		case BFTW_CONTINUE:
			break;
		case BFTW_PRUNE:
			gcflags &= ~BFTW_VISIT_FILE;
			goto next;
		case BFTW_STOP:
			goto done;
		}

		bftw_opendir(&state);

		bftw_batch_start(&state);
		while (bftw_readdir(&state) > 0) {
			if (bftw_push(&state, state.de->name, false) != 0) {
				goto done;
			}
		}
		bftw_batch_finish(&state);

		if (bftw_closedir(&state, gcflags) == BFTW_STOP) {
			goto done;
		}

	next:
		if (bftw_gc_file(&state, gcflags) == BFTW_STOP) {
			goto done;
		}
	}

done:
	return bftw_state_destroy(&state);
}

/** Select bftw_stream() or bftw_batch() appropriately. */
static int bftw_auto(const struct bftw_args *args) {
	if (args->flags & BFTW_SORT) {
		return bftw_batch(args);
	} else {
		return bftw_stream(args);
	}
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
	ids_args->strategy = BFTW_DFS;
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

		if (bftw_auto(&ids_args) != 0) {
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

			if (bftw_auto(&ids_args) != 0) {
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

		if (bftw_auto(&ids_args) != 0) {
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

		if (bftw_auto(&ids_args) != 0) {
			state.error = errno;
		}
	}

	return bftw_ids_finish(&state);
}

int bftw(const struct bftw_args *args) {
	switch (args->strategy) {
	case BFTW_BFS:
		return bftw_auto(args);
	case BFTW_DFS:
		return bftw_batch(args);
	case BFTW_IDS:
		return bftw_ids(args);
	case BFTW_EDS:
		return bftw_eds(args);
	}

	errno = EINVAL;
	return -1;
}
