/****************************************************************************
 * bfs                                                                      *
 * Copyright (C) 2015-2017 Tavian Barnes <tavianator@tavianator.com>        *
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
 * - struct bftw_state: Represents the current state of the traversal, allowing
 *   bftw() to be factored into various helper functions.
 */

#include "bftw.h"
#include "dstring.h"
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
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/**
 * A directory.
 */
struct bftw_dir {
	/** The parent directory, if any. */
	struct bftw_dir *parent;
	/** This directory's depth in the walk. */
	size_t depth;

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

		bftw_heap_move(cache, child, i);
		i = ci;
	}

	bftw_heap_move(cache, dir, i);
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
		bftw_heap_bubble_down(cache, end);
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

/** Pop a directory other than 'saved' from the cache. */
static void bftw_cache_pop_other(struct bftw_cache *cache, const struct bftw_dir *saved) {
	assert(cache->size > 1);

	struct bftw_dir *dir = cache->heap[0];
	if (dir == saved) {
		dir = cache->heap[1];
	}

	bftw_dir_close(cache, dir);
}

/** Create a new bftw_dir. */
static struct bftw_dir *bftw_dir_new(struct bftw_cache *cache, struct bftw_dir *parent, const char *name) {
	size_t namelen = strlen(name);
	size_t size = sizeof(struct bftw_dir) + namelen + 1;

	bool needs_slash = false;
	if (namelen == 0 || name[namelen - 1] != '/') {
		needs_slash = true;
		++size;
	}

	struct bftw_dir *dir = malloc(size);
	if (!dir) {
		return NULL;
	}

	dir->parent = parent;

	if (parent) {
		dir->depth = parent->depth + 1;
		dir->nameoff = parent->nameoff + parent->namelen;
		bftw_dir_incref(cache, parent);
	} else {
		dir->depth = 0;
		dir->nameoff = 0;
	}

	dir->refcount = 1;
	dir->fd = -1;

	dir->dev = -1;
	dir->ino = -1;

	memcpy(dir->name, name, namelen);
	if (needs_slash) {
		dir->name[namelen++] = '/';
	}
	dir->name[namelen] = '\0';
	dir->namelen = namelen;

	return dir;
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
		*at_path += base->nameoff + base->namelen;
	}

	return base;
}

/**
 * Check if we should retry an operation due to EMFILE.
 *
 * @param cache
 *         The cache in question.
 * @param saved
 *         A bftw_dir that must be preserved.
 */
static bool bftw_should_retry(struct bftw_cache *cache, const struct bftw_dir *saved) {
	if (errno == EMFILE && cache->size > 1) {
		// Too many open files, shrink the cache
		bftw_cache_pop_other(cache, saved);
		cache->capacity = cache->size;
		return true;
	} else {
		return false;
	}
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
 *         The base file descriptor, AT_CWDFD if base == NULL.
 * @param at_path
 *         The relative path to the dir.
 * @return
 *         The opened file descriptor, or negative on error.
 */
static int bftw_dir_openat(struct bftw_cache *cache, struct bftw_dir *dir, const struct bftw_dir *base, int at_fd, const char *at_path) {
	assert(dir->fd < 0);

	int flags = O_RDONLY | O_CLOEXEC | O_DIRECTORY;
	int fd = openat(at_fd, at_path, flags);

	if (fd < 0 && bftw_should_retry(cache, base)) {
		fd = openat(at_fd, at_path, flags);
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

	if (dfd < 0 && bftw_should_retry(cache, dir)) {
		dfd = dup_cloexec(fd);
	}
	if (dfd < 0) {
		return NULL;
	}

	DIR *ret = fdopendir(dfd);
	if (!ret) {
		close(dfd);
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
	/** The circular buffer of directories. */
	struct bftw_dir **buffer;
	/** The head of the queue. */
	size_t head;
	/** The size of the queue. */
	size_t size;
	/** The capacity of the queue (always a power of two). */
	size_t capacity;
};

/** Initialize a bftw_queue. */
static int bftw_queue_init(struct bftw_queue *queue) {
	queue->head = 0;
	queue->size = 0;
	queue->capacity = 256;
	queue->buffer = malloc(queue->capacity*sizeof(*queue->buffer));
	if (queue->buffer) {
		return 0;
	} else {
		return -1;
	}
}

/** Add a directory to the bftw_queue. */
static int bftw_queue_push(struct bftw_queue *queue, struct bftw_dir *dir) {
	struct bftw_dir **buffer = queue->buffer;
	size_t head = queue->head;
	size_t size = queue->size;
	size_t tail = head + size;
	size_t capacity = queue->capacity;

	if (size == capacity) {
		capacity *= 2;
		buffer = realloc(buffer, capacity*sizeof(struct bftw_dir *));
		if (!buffer) {
			return -1;
		}

		for (size_t i = size; i < tail; ++i) {
			buffer[i] = buffer[i - size];
		}

		queue->buffer = buffer;
		queue->capacity = capacity;
	}

	tail &= capacity - 1;
	buffer[tail] = dir;
	++queue->size;
	return 0;
}

/** Remove a directory from the bftw_queue. */
static struct bftw_dir *bftw_queue_pop(struct bftw_queue *queue) {
	if (queue->size == 0) {
		return NULL;
	}

	struct bftw_dir *dir = queue->buffer[queue->head];
	--queue->size;
	++queue->head;
	queue->head &= queue->capacity - 1;
	return dir;
}

/** Destroy a bftw_queue. */
static void bftw_queue_destroy(struct bftw_queue *queue) {
	assert(queue->size == 0);
	free(queue->buffer);
}

/** Call stat() and use the results. */
static int ftwbuf_stat(struct BFTW *ftwbuf, struct stat *sb) {
	int ret = fstatat(ftwbuf->at_fd, ftwbuf->at_path, sb, ftwbuf->at_flags);
	if (ret != 0) {
		return ret;
	}

	ftwbuf->statbuf = sb;
	ftwbuf->typeflag = mode_to_typeflag(sb->st_mode);

	return 0;
}

/**
 * Possible bftw() traversal statuses.
 */
enum bftw_status {
	/** The current path is state.current. */
	BFTW_CURRENT,
	/** The current path is a child of state.current. */
	BFTW_CHILD,
	/** bftw_dir's are being garbage collected. */
	BFTW_GC,
};

/**
 * Holds the current state of the bftw() traversal.
 */
struct bftw_state {
	/** bftw() callback. */
	bftw_fn *fn;
	/** bftw() flags. */
	int flags;
	/** bftw() callback data. */
	void *ptr;

	/** The appropriate errno value, if any. */
	int error;

	/** The cache of open directories. */
	struct bftw_cache cache;

	/** The queue of directories left to explore. */
	struct bftw_queue queue;
	/** The current directory. */
	struct bftw_dir *current;
	/** The previous directory. */
	struct bftw_dir *last;
	/** The currently open directory. */
	DIR *dir;
	/** The current traversal status. */
	enum bftw_status status;

	/** The root path of the walk. */
	const char *root;
	/** The current path being explored. */
	char *path;

	/** Extra data about the current file. */
	struct BFTW ftwbuf;
	/** stat() buffer for the current file. */
	struct stat statbuf;
};

/**
 * Initialize the bftw() state.
 */
static int bftw_state_init(struct bftw_state *state, const char *root, bftw_fn *fn, int nopenfd, int flags, void *ptr) {
	state->root = root;
	state->fn = fn;
	state->flags = flags;
	state->ptr = ptr;

	state->error = 0;

	if (nopenfd < 2) {
		errno = EMFILE;
		return -1;
	}

	// -1 to account for dup()
	if (bftw_cache_init(&state->cache, nopenfd - 1) != 0) {
		goto err;
	}

	if (bftw_queue_init(&state->queue) != 0) {
		goto err_cache;
	}

	state->current = NULL;
	state->last = NULL;
	state->dir = NULL;
	state->status = BFTW_CURRENT;

	state->path = dstralloc(0);
	if (!state->path) {
		goto err_queue;
	}

	return 0;

err_queue:
	bftw_queue_destroy(&state->queue);
err_cache:
	bftw_cache_destroy(&state->cache);
err:
	return -1;
}

/**
 * Compute the path to the current bftw_dir.
 */
static int bftw_build_path(struct bftw_state *state) {
	const struct bftw_dir *dir = state->current;
	size_t namelen = dir->namelen;
	size_t pathlen = dir->nameoff + namelen;

	if (dstresize(&state->path, pathlen) != 0) {
		return -1;
	}

	// Only rebuild the part of the path that changes
	const struct bftw_dir *last = state->last;
	while (last && last->depth > dir->depth) {
		last = last->parent;
	}

	// Build the path backwards
	char *path = state->path;
	while (dir != last) {
		char *segment = path + dir->nameoff;
		namelen = dir->namelen;
		memcpy(segment, dir->name, namelen);

		if (last && last->depth == dir->depth) {
			last = last->parent;
		}
		dir = dir->parent;
	}

	state->last = state->current;

	return 0;
}

/**
 * Concatenate a subpath to the current path.
 */
static int bftw_path_concat(struct bftw_state *state, const char *subpath) {
	size_t nameoff = 0;

	struct bftw_dir *current = state->current;
	if (current) {
		nameoff = current->nameoff + current->namelen;
	}

	state->status = BFTW_CHILD;

	if (dstresize(&state->path, nameoff) != 0) {
		return -1;
	}
	return dstrcat(&state->path, subpath);
}

/**
 * Trim the path to just the current directory.
 */
static void bftw_path_trim(struct bftw_state *state) {
	struct bftw_dir *current = state->current;

	size_t length;
	if (current->depth == 0) {
		// Use exactly the string passed to bftw(), including any
		// trailing slashes
		length = strlen(state->root);
	} else {
		length = current->nameoff + current->namelen;
		if (current->namelen > 1) {
			// Trim the trailing slash
			--length;
			state->last = current->parent;
		}
	}
	dstresize(&state->path, length);

	if (state->status == BFTW_CHILD) {
		state->status = BFTW_CURRENT;
	}
}

/**
 * Open the current directory.
 */
static int bftw_opendir(struct bftw_state *state) {
	assert(!state->dir);

	state->dir = bftw_dir_opendir(&state->cache, state->current, state->path);
	if (state->dir) {
		return 0;
	} else {
		return 1;
	}
}

/**
 * Close the current directory.
 */
static int bftw_closedir(struct bftw_state *state) {
	DIR *dir = state->dir;
	state->dir = NULL;
	if (dir) {
		return closedir(dir);
	} else {
		return 0;
	}
}

/**
 * Record an error.
 */
static void bftw_set_error(struct bftw_state *state, int error) {
	state->ftwbuf.error = error;
	state->ftwbuf.typeflag = BFTW_ERROR;

	if (!(state->flags & BFTW_RECOVER)) {
		state->error = error;
	}
}

/**
 * Initialize the buffers with data about the current path.
 */
static void bftw_init_buffers(struct bftw_state *state, const struct dirent *de) {
	struct BFTW *ftwbuf = &state->ftwbuf;
	ftwbuf->path = state->path;
	ftwbuf->root = state->root;
	ftwbuf->error = 0;
	ftwbuf->visit = (state->status == BFTW_GC ? BFTW_POST : BFTW_PRE);
	ftwbuf->statbuf = NULL;
	ftwbuf->at_fd = AT_FDCWD;
	ftwbuf->at_path = ftwbuf->path;

	struct bftw_dir *current = state->current;
	if (current) {
		ftwbuf->nameoff = current->nameoff;
		ftwbuf->depth = current->depth;

		if (state->status == BFTW_CHILD) {
			ftwbuf->nameoff += current->namelen;
			++ftwbuf->depth;

			ftwbuf->at_fd = current->fd;
			ftwbuf->at_path += ftwbuf->nameoff;
		} else {
			bftw_dir_base(current, &ftwbuf->at_fd, &ftwbuf->at_path);
		}
	} else {
		ftwbuf->depth = 0;
	}

	if (ftwbuf->depth == 0) {
		// Compute the name offset for root paths like "foo/bar"
		ftwbuf->nameoff = xbasename(ftwbuf->path) - ftwbuf->path;
	}

	ftwbuf->typeflag = BFTW_UNKNOWN;
	if (de) {
		ftwbuf->typeflag = dirent_to_typeflag(de);
	} else if (state->status != BFTW_CHILD) {
		ftwbuf->typeflag = BFTW_DIR;
	}

	int follow_flags = BFTW_LOGICAL;
	if (ftwbuf->depth == 0) {
		follow_flags |= BFTW_COMFOLLOW;
	}
	bool follow = state->flags & follow_flags;
	ftwbuf->at_flags = follow ? 0 : AT_SYMLINK_NOFOLLOW;

	bool detect_cycles = (state->flags & BFTW_DETECT_CYCLES)
		&& state->status == BFTW_CHILD;

	bool xdev = state->flags & BFTW_XDEV;

	if ((state->flags & BFTW_STAT)
	    || ftwbuf->typeflag == BFTW_UNKNOWN
	    || (ftwbuf->typeflag == BFTW_LNK && follow)
	    || (ftwbuf->typeflag == BFTW_DIR && (detect_cycles || xdev))) {
		int ret = ftwbuf_stat(ftwbuf, &state->statbuf);
		if (ret != 0 && follow && (errno == ENOENT || errno == ENOTDIR)) {
			// Could be a broken symlink, retry without following
			ftwbuf->at_flags = AT_SYMLINK_NOFOLLOW;
			ret = ftwbuf_stat(ftwbuf, &state->statbuf);
		}

		if (ret != 0) {
			bftw_set_error(state, errno);
			return;
		}

		if (ftwbuf->typeflag == BFTW_DIR && detect_cycles) {
			dev_t dev = ftwbuf->statbuf->st_dev;
			ino_t ino = ftwbuf->statbuf->st_ino;
			for (const struct bftw_dir *dir = current; dir; dir = dir->parent) {
				if (dev == dir->dev && ino == dir->ino) {
					bftw_set_error(state, ELOOP);
					return;
				}
			}
		}
	}
}

/** internal action: Abort the traversal. */
#define BFTW_FAIL (-1)

/**
 * Invoke the callback on the current file.
 */
static int bftw_handle_path(struct bftw_state *state) {
	// Never give the callback BFTW_ERROR unless BFTW_RECOVER is specified
	if (state->ftwbuf.typeflag == BFTW_ERROR && !(state->flags & BFTW_RECOVER)) {
		return BFTW_FAIL;
	}

	// Defensive copy
	struct BFTW ftwbuf = state->ftwbuf;

	enum bftw_action action = state->fn(&ftwbuf, state->ptr);
	switch (action) {
	case BFTW_CONTINUE:
	case BFTW_SKIP_SIBLINGS:
	case BFTW_SKIP_SUBTREE:
	case BFTW_STOP:
		return action;

	default:
		state->error = EINVAL;
		return BFTW_FAIL;
	}
}

/**
 * Create a new bftw_dir for the current file.
 */
static struct bftw_dir *bftw_add(struct bftw_state *state, const char *name) {
	struct bftw_dir *dir = bftw_dir_new(&state->cache, state->current, name);
	if (!dir) {
		return NULL;
	}

	const struct stat *statbuf = state->ftwbuf.statbuf;
	if (statbuf) {
		dir->dev = statbuf->st_dev;
		dir->ino = statbuf->st_ino;
	}

	return dir;
}

/**
 * Garbage-collect a bftw_dir.
 */
static int bftw_gc(struct bftw_state *state, struct bftw_dir *dir, bool invoke_callback) {
	int ret = BFTW_CONTINUE;

	if (!(state->flags & BFTW_DEPTH)) {
		invoke_callback = false;
	}

	if (invoke_callback) {
		if (bftw_build_path(state) != 0) {
			ret = BFTW_FAIL;
			invoke_callback = false;
		}
	}

	state->status = BFTW_GC;

	while (dir) {
		bftw_dir_decref(&state->cache, dir);
		if (dir->refcount > 0) {
			break;
		}

		if (invoke_callback) {
			state->current = dir;
			bftw_path_trim(state);
			bftw_init_buffers(state, NULL);

			int action = bftw_handle_path(state);
			switch (action) {
			case BFTW_CONTINUE:
			case BFTW_SKIP_SIBLINGS:
			case BFTW_SKIP_SUBTREE:
				break;

			case BFTW_STOP:
			case BFTW_FAIL:
				ret = action;
				invoke_callback = false;
				break;
			}
		}

		struct bftw_dir *parent = dir->parent;
		bftw_dir_free(&state->cache, dir);
		dir = parent;
	}

	state->last = dir;
	return ret;
}

/**
 * Push a new directory onto the queue.
 */
static int bftw_push(struct bftw_state *state, const char *name) {
	struct bftw_dir *dir = bftw_add(state, name);
	if (!dir) {
		return -1;
	}

	if (bftw_queue_push(&state->queue, dir) != 0) {
		state->error = errno;
		bftw_gc(state, dir, false);
		return -1;
	}

	return 0;
}

/**
 * Pop a directory off the queue.
 */
static int bftw_pop(struct bftw_state *state, bool invoke_callback) {
	int ret = bftw_gc(state, state->current, invoke_callback);
	state->current = bftw_queue_pop(&state->queue);
	state->status = BFTW_CURRENT;
	return ret;
}

/**
 * Dispose of the bftw() state.
 */
static void bftw_state_destroy(struct bftw_state *state) {
	bftw_closedir(state);

	while (state->current) {
		bftw_pop(state, false);
	}
	bftw_queue_destroy(&state->queue);

	bftw_cache_destroy(&state->cache);

	dstrfree(state->path);
}

int bftw(const char *path, bftw_fn *fn, int nopenfd, enum bftw_flags flags, void *ptr) {
	int ret = -1, error;

	struct bftw_state state;
	if (bftw_state_init(&state, path, fn, nopenfd, flags, ptr) != 0) {
		return -1;
	}

	// Handle 'path' itself first

	if (bftw_path_concat(&state, path) != 0) {
		goto fail;
	}

	bftw_init_buffers(&state, NULL);

	switch (bftw_handle_path(&state)) {
	case BFTW_CONTINUE:
	case BFTW_SKIP_SIBLINGS:
		break;

	case BFTW_SKIP_SUBTREE:
	case BFTW_STOP:
		goto done;

	case BFTW_FAIL:
		goto fail;
	}

	if (state.ftwbuf.typeflag != BFTW_DIR) {
		goto done;
	}

	// Now start the breadth-first search

	state.current = bftw_add(&state, path);
	if (!state.current) {
		goto fail;
	}

	do {
		if (bftw_build_path(&state) != 0) {
			goto fail;
		}

		if (bftw_opendir(&state) != 0) {
			goto dir_error;
		}

		while (true) {
			struct dirent *de;
			if (xreaddir(state.dir, &de) != 0) {
				goto dir_error;
			}
			if (!de) {
				break;
			}

			if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) {
				continue;
			}

			if (bftw_path_concat(&state, de->d_name) != 0) {
				goto fail;
			}

			bftw_init_buffers(&state, de);

			switch (bftw_handle_path(&state)) {
			case BFTW_CONTINUE:
				break;

			case BFTW_SKIP_SIBLINGS:
				goto next;

			case BFTW_SKIP_SUBTREE:
				continue;

			case BFTW_STOP:
				goto done;

			case BFTW_FAIL:
				goto fail;
			}

			if (state.ftwbuf.typeflag == BFTW_DIR) {
				const struct stat *statbuf = state.ftwbuf.statbuf;
				if ((flags & BFTW_XDEV)
				    && statbuf
				    && statbuf->st_dev != state.current->dev) {
					continue;
				}

				if (bftw_push(&state, de->d_name) != 0) {
					goto fail;
				}
			}
		}

	next:
		if (bftw_closedir(&state) != 0) {
			goto dir_error;
		}

		switch (bftw_pop(&state, true)) {
		case BFTW_CONTINUE:
		case BFTW_SKIP_SIBLINGS:
		case BFTW_SKIP_SUBTREE:
			break;

		case BFTW_STOP:
			goto done;

		case BFTW_FAIL:
			goto fail;
		}
		continue;

	dir_error:
		error = errno;
		bftw_closedir(&state);
		bftw_path_trim(&state);
		bftw_init_buffers(&state, NULL);
		bftw_set_error(&state, error);

		switch (bftw_handle_path(&state)) {
		case BFTW_CONTINUE:
		case BFTW_SKIP_SIBLINGS:
		case BFTW_SKIP_SUBTREE:
			goto next;

		case BFTW_STOP:
			goto done;

		case BFTW_FAIL:
			goto fail;
		}
	} while (state.current);

done:
	if (state.error == 0) {
		ret = 0;
	}

fail:
	if (state.error == 0) {
		state.error = errno;
	}

	bftw_state_destroy(&state);

	errno = state.error;
	return ret;
}
