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
	enum bftw_typeflag typeflag;
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
	assert(file->fd >= 0);

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
	size_t size = sizeof(struct bftw_file) + namelen + 1;

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

	file->typeflag = BFTW_UNKNOWN;
	file->dev = -1;
	file->ino = -1;

	file->namelen = namelen;
	memcpy(file->name, name, namelen + 1);

	return file;
}

/**
 * Get the appropriate (fd, path) pair for the *at() family of functions.
 *
 * @param file
 *         The file being accessed.
 * @param[out] at_fd
 *         Will hold the appropriate file descriptor to use.
 * @param[in,out] at_path
 *         Will hold the appropriate path to use.
 * @return The closest open ancestor file.
 */
static struct bftw_file *bftw_file_base(struct bftw_file *file, int *at_fd, const char **at_path) {
	struct bftw_file *base = file;

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
static int bftw_file_openat(struct bftw_cache *cache, struct bftw_file *file, const struct bftw_file *base, int at_fd, const char *at_path) {
	assert(file->fd < 0);

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
	int at_fd = AT_FDCWD;
	const char *at_path = path;
	struct bftw_file *base = bftw_file_base(file, &at_fd, &at_path);

	int fd = bftw_file_openat(cache, file, base, at_fd, at_path);
	if (fd >= 0 || errno != ENAMETOOLONG) {
		return fd;
	}

	// Handle ENAMETOOLONG by manually traversing the path component-by-component

	// -1 to include the root, which has depth == 0
	size_t offset = base ? base->depth : -1;
	size_t levels = file->depth - offset;
	if (levels < 2) {
		return fd;
	}

	struct bftw_file **parents = malloc(levels * sizeof(*parents));
	if (!parents) {
		return fd;
	}

	struct bftw_file *parent = file;
	for (size_t i = levels; i-- > 0;) {
		parents[i] = parent;
		parent = parent->parent;
	}

	for (size_t i = 0; i < levels; ++i) {
		fd = bftw_file_openat(cache, parents[i], base, at_fd, parents[i]->name);
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
 * Open a DIR* for a bftw_file.
 *
 * @param cache
 *         The cache to hold the file.
 * @param file
 *         The directory to open.
 * @param path
 *         The full path to the directory.
 * @return
 *         The opened DIR *, or NULL on error.
 */
static DIR *bftw_file_opendir(struct bftw_cache *cache, struct bftw_file *file, const char *path) {
	int fd = bftw_file_open(cache, file, path);
	if (fd < 0) {
		return NULL;
	}

	// Now we dup() the fd and pass it to fdopendir().  This way we can
	// close the DIR* as soon as we're done with it, reducing the memory
	// footprint significantly, while keeping the fd around for future
	// openat() calls.

	int dfd = dup_cloexec(fd);

	if (dfd < 0 && errno == EMFILE) {
		if (bftw_cache_shrink(cache, file) == 0) {
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
 * A directory reader.
 */
struct bftw_reader {
	/** The open handle to the directory. */
	DIR *dir;
	/** The current directory entry. */
	struct dirent *de;
	/** Any error code that has occurred. */
	int error;
};

/** Initialize a reader. */
static void bftw_reader_init(struct bftw_reader *reader) {
	reader->dir = NULL;
	reader->de = NULL;
	reader->error = 0;
}

/** Open a directory for reading. */
static int bftw_reader_open(struct bftw_reader *reader, struct bftw_cache *cache, struct bftw_file *file, const char *path) {
	assert(!reader->dir);
	assert(!reader->de);

	reader->error = 0;

	reader->dir = bftw_file_opendir(cache, file, path);
	if (!reader->dir) {
		reader->error = errno;
		return -1;
	}

	return 0;
}

/** Read a directory entry. */
static int bftw_reader_read(struct bftw_reader *reader) {
	if (!reader->dir) {
		return -1;
	}

	if (xreaddir(reader->dir, &reader->de) != 0) {
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
	if (reader->dir && closedir(reader->dir) != 0) {
		reader->error = errno;
		ret = -1;
	}

	reader->de = NULL;
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
	/** The start of the current batch of files. */
	struct bftw_file **batch;

	/** The current path. */
	char *path;
	/** The current file. */
	struct bftw_file *file;
	/** The previous file. */
	struct bftw_file *previous;
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
	state->batch = NULL;

	state->path = dstralloc(0);
	if (!state->path) {
		goto err_cache;
	}

	state->file = NULL;
	state->previous = NULL;
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
	if (ftwbuf->typeflag == BFTW_UNKNOWN) {
		return true;
	}

	if (ftwbuf->typeflag == BFTW_LNK && !(ftwbuf->stat_flags & BFS_STAT_NOFOLLOW)) {
		return true;
	}

	if (ftwbuf->typeflag == BFTW_DIR) {
		if (state->flags & (BFTW_DETECT_CYCLES | BFTW_MOUNT | BFTW_XDEV)) {
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
	const struct bftw_reader *reader = &state->reader;
	const struct dirent *de = reader->de;

	struct BFTW *ftwbuf = &state->ftwbuf;
	ftwbuf->path = state->path;
	ftwbuf->root = file ? file->root->name : ftwbuf->path;
	ftwbuf->depth = 0;
	ftwbuf->visit = visit;
	ftwbuf->typeflag = BFTW_UNKNOWN;
	ftwbuf->error = reader->error;
	ftwbuf->at_fd = AT_FDCWD;
	ftwbuf->at_path = ftwbuf->path;
	ftwbuf->stat_flags = BFS_STAT_NOFOLLOW;
	bftw_stat_init(&ftwbuf->lstat_cache);
	bftw_stat_init(&ftwbuf->stat_cache);

	struct bftw_file *parent = NULL;
	if (de) {
		parent = file;
		ftwbuf->depth = file->depth + 1;
		ftwbuf->typeflag = bftw_dirent_typeflag(de);
		ftwbuf->nameoff = bftw_child_nameoff(file);
	} else if (file) {
		parent = file->parent;
		ftwbuf->depth = file->depth;
		ftwbuf->typeflag = file->typeflag;
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
		ftwbuf->typeflag = BFTW_ERROR;
		return;
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
		for (const struct bftw_file *ancestor = parent; ancestor; ancestor = ancestor->parent) {
			if (ancestor->dev == statbuf->dev && ancestor->ino == statbuf->ino) {
				ftwbuf->typeflag = BFTW_ERROR;
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

	// Never give the callback BFTW_ERROR unless BFTW_RECOVER is specified
	if (ftwbuf->typeflag == BFTW_ERROR && !(state->flags & BFTW_RECOVER)) {
		state->error = ftwbuf->error;
		return BFTW_STOP;
	}

	if ((state->flags & BFTW_MOUNT) && bftw_is_mount(state, name)) {
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

	if (visit != BFTW_PRE || ftwbuf->typeflag != BFTW_DIR) {
		ret = BFTW_PRUNE;
		goto done;
	}

	if ((state->flags & BFTW_XDEV) && bftw_is_mount(state, name)) {
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

	struct dirent *de = state->reader.de;
	if (de) {
		file->typeflag = bftw_dirent_typeflag(de);
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
 * Open a reader for the current directory.
 */
static struct bftw_reader *bftw_open(struct bftw_state *state) {
	struct bftw_reader *reader = &state->reader;
	bftw_reader_open(reader, &state->cache, state->file, state->path);
	return reader;
}

/**
 * Flags controlling which files get visited when releasing a reader/file.
 */
enum bftw_release_flags {
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
 * Close and release the reader.
 */
static enum bftw_action bftw_release_reader(struct bftw_state *state, enum bftw_release_flags flags) {
	enum bftw_action ret = BFTW_CONTINUE;

	struct bftw_reader *reader = &state->reader;
	bftw_reader_close(reader);

	if (reader->error != 0) {
		if (flags & BFTW_VISIT_FILE) {
			ret = bftw_visit(state, NULL, BFTW_PRE);
		} else {
			state->error = reader->error;
		}
		reader->error = 0;
	}

	return ret;
}

/**
 * Finalize and free a file we're done with.
 */
static enum bftw_action bftw_release_file(struct bftw_state *state, enum bftw_release_flags flags) {
	enum bftw_action ret = BFTW_CONTINUE;

	if (!(state->flags & BFTW_DEPTH)) {
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
		bftw_release_file(state, BFTW_VISIT_NONE);
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

	bftw_release_reader(state, BFTW_VISIT_NONE);

	bftw_release_file(state, BFTW_VISIT_NONE);
	bftw_drain_queue(state, &state->queue);

	bftw_cache_destroy(&state->cache);

	errno = state->error;
	return state->error ? -1 : 0;
}

/**
 * Streaming mode: visit files as they are encountered.
 */
static int bftw_stream(const struct bftw_args *args) {
	struct bftw_state state;
	if (bftw_state_init(&state, args) != 0) {
		return -1;
	}

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

	while (bftw_pop(&state) > 0) {
		struct bftw_reader *reader = bftw_open(&state);

		while (bftw_reader_read(reader) > 0) {
			const char *name = reader->de->d_name;

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

		if (bftw_release_reader(&state, BFTW_VISIT_ALL) == BFTW_STOP) {
			goto done;
		}
		if (bftw_release_file(&state, BFTW_VISIT_ALL) == BFTW_STOP) {
			goto done;
		}
	}

done:
	return bftw_state_destroy(&state);
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
		enum bftw_release_flags relflags = BFTW_VISIT_ALL;

		switch (bftw_visit(&state, NULL, BFTW_PRE)) {
		case BFTW_CONTINUE:
			break;
		case BFTW_PRUNE:
			relflags &= ~BFTW_VISIT_FILE;
			goto next;
		case BFTW_STOP:
			goto done;
		}

		struct bftw_reader *reader = bftw_open(&state);

		bftw_batch_start(&state);
		while (bftw_reader_read(reader) > 0) {
			if (bftw_push(&state, reader->de->d_name, false) != 0) {
				goto done;
			}
		}
		bftw_batch_finish(&state);

		if (bftw_release_reader(&state, relflags) == BFTW_STOP) {
			goto done;
		}

	next:
		if (bftw_release_file(&state, relflags) == BFTW_STOP) {
			goto done;
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
			return BFTW_PRUNE;
		}
	}

	if (ftwbuf->depth < state->depth) {
		if (trie_find_str(state->pruned, ftwbuf->path)) {
			return BFTW_PRUNE;
		} else {
			return BFTW_CONTINUE;
		}
	} else if (state->visit == BFTW_POST) {
		if (trie_find_str(state->pruned, ftwbuf->path)) {
			return BFTW_PRUNE;
		}
	}

	state->bottom = false;

	enum bftw_action ret = state->delegate(ftwbuf, state->ptr);

	switch (ret) {
	case BFTW_CONTINUE:
		ret = BFTW_PRUNE;
		break;
	case BFTW_PRUNE:
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
		ret = args->flags & BFTW_SORT ? bftw_batch(&ids_args) : bftw_stream(&ids_args);
		++state.depth;
	}

	if (args->flags & BFTW_DEPTH) {
		state.visit = BFTW_POST;

		while (ret == 0 && !state.quit && state.depth > 0) {
			--state.depth;
			ret = args->flags & BFTW_SORT ? bftw_batch(&ids_args) : bftw_stream(&ids_args);
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
		if (!(args->flags & BFTW_SORT)) {
			return bftw_stream(args);
		}
		// Fallthrough
	case BFTW_DFS:
		return bftw_batch(args);
	case BFTW_IDS:
		return bftw_ids(args);
	}

	errno = EINVAL;
	return -1;
}
