/*********************************************************************
 * bfs                                                               *
 * Copyright (C) 2015 Tavian Barnes <tavianator@tavianator.com>      *
 *                                                                   *
 * This program is free software. It comes without any warranty, to  *
 * the extent permitted by applicable law. You can redistribute it   *
 * and/or modify it under the terms of the Do What The Fuck You Want *
 * To Public License, Version 2, as published by Sam Hocevar. See    *
 * the COPYING file or http://www.wtfpl.net/ for more details.       *
 *********************************************************************/

/**
 * bftw() implementation.
 *
 * The goal of this implementation is to avoid re-traversal by using openat() as
 * much as possible.  Since the number of open file descriptors is limited, the
 * 'dircache' maintains a priority queue of open 'dircache_entry's, ordered by
 * their reference counts to keep the most-referenced parent directories open.
 *
 * The 'dirqueue' is a simple FIFO of 'dircache_entry's left to explore.
 */

#include "bftw.h"
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
 * Simple dynamically-sized string type.
 */
struct dynstr {
	char *str;
	size_t length;
	size_t capacity;
};

/** Initialize a dynstr. */
static void dynstr_init(struct dynstr *dstr) {
	dstr->str = NULL;
	dstr->length = 0;
	dstr->capacity = 0;
}

/** Grow a dynstr to the given capacity if necessary. */
static int dynstr_grow(struct dynstr *dstr, size_t length) {
	if (length >= dstr->capacity) {
		size_t new_capacity = 3*(length + 1)/2;
		char *new_str = realloc(dstr->str, new_capacity);
		if (!new_str) {
			return -1;
		}

		dstr->str = new_str;
		dstr->capacity = new_capacity;
	}

	return 0;
}

/** Concatenate a string to a dynstr at the given position. */
static int dynstr_concat(struct dynstr *dstr, size_t pos, const char *more) {
	size_t morelen = strlen(more);
	size_t length = pos + morelen;
	if (dynstr_grow(dstr, length) != 0) {
		return -1;
	}

	memcpy(dstr->str + pos, more, morelen + 1);
	dstr->length = length;
	return 0;
}

/** Free a dynstr. */
static void dynstr_free(struct dynstr *dstr) {
	free(dstr->str);
}

/**
 * A single entry in the dircache.
 */
struct dircache_entry {
	/** The parent entry, if any. */
	struct dircache_entry *parent;
	/** This directory's depth in the walk. */
	size_t depth;

	/** Reference count. */
	size_t refcount;
	/** Index in the priority queue. */
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
 * A directory cache.
 */
struct dircache {
	/** A min-heap of open entries, ordered by refcount. */
	struct dircache_entry **heap;
	/** Current heap size. */
	size_t size;
	/** Maximum heap size. */
	size_t capacity;
};

/** Initialize a dircache. */
static int dircache_init(struct dircache *cache, size_t capacity) {
	cache->heap = malloc(capacity*sizeof(struct dircache_entry *));
	if (!cache->heap) {
		return -1;
	}

	cache->size = 0;
	cache->capacity = capacity;
	return 0;
}

/** Destroy a dircache. */
static void dircache_free(struct dircache *cache) {
	assert(cache->size == 0);
	free(cache->heap);
}

/** Move an entry to a particular place in the heap. */
static void dircache_heap_move(struct dircache *cache, struct dircache_entry *entry, size_t i) {
	cache->heap[i] = entry;
	entry->heap_index = i;
}

/** Bubble an entry up the heap. */
static void dircache_bubble_up(struct dircache *cache, struct dircache_entry *entry) {
	size_t i = entry->heap_index;

	while (i > 0) {
		size_t pi = (i - 1)/2;
		struct dircache_entry *parent = cache->heap[pi];
		if (entry->refcount >= parent->refcount) {
			break;
		}

		dircache_heap_move(cache, parent, i);
		i = pi;
	}

	dircache_heap_move(cache, entry, i);
}

/** Bubble an entry down the heap. */
static void dircache_bubble_down(struct dircache *cache, struct dircache_entry *entry) {
	size_t i = entry->heap_index;

	while (true) {
		size_t ci = 2*i + 1;
		if (ci >= cache->size) {
			break;
		}

		struct dircache_entry *child = cache->heap[ci];

		size_t ri = ci + 1;
		if (ri < cache->size) {
			struct dircache_entry *right = cache->heap[ri];
			if (child->refcount > right->refcount) {
				ci = ri;
				child = right;
			}
		}

		dircache_heap_move(cache, child, i);
		i = ci;
	}

	dircache_heap_move(cache, entry, i);
}

/** Increment a dircache_entry's reference count. */
static void dircache_entry_incref(struct dircache *cache, struct dircache_entry *entry) {
	++entry->refcount;

	if (entry->fd >= 0) {
		dircache_bubble_down(cache, entry);
	}
}

/** Decrement a dircache_entry's reference count. */
static void dircache_entry_decref(struct dircache *cache, struct dircache_entry *entry) {
	--entry->refcount;

	if (entry->fd >= 0) {
		dircache_bubble_up(cache, entry);
	}
}

/** Add a dircache_entry to the priority queue. */
static void dircache_push(struct dircache *cache, struct dircache_entry *entry) {
	assert(cache->size < cache->capacity);
	assert(entry->fd >= 0);

	size_t size = cache->size++;
	entry->heap_index = size;
	dircache_bubble_up(cache, entry);
}

/** Close a dircache_entry and remove it from the priority queue. */
static void dircache_pop(struct dircache *cache, struct dircache_entry *entry) {
	assert(cache->size > 0);
	assert(entry->fd >= 0);

	close(entry->fd);
	entry->fd = -1;

	size_t size = --cache->size;
	size_t i = entry->heap_index;
	if (i != size) {
		struct dircache_entry *end = cache->heap[size];
		end->heap_index = i;
		dircache_bubble_down(cache, end);
	}
}

/** Add an entry to the dircache. */
static struct dircache_entry *dircache_add(struct dircache *cache, struct dircache_entry *parent, const char *name) {
	size_t namelen = strlen(name);
	size_t size = sizeof(struct dircache_entry) + namelen;

	bool needs_slash = false;
	if (namelen == 0 || name[namelen - 1] != '/') {
		needs_slash = true;
		++size;
	}

	struct dircache_entry *entry = malloc(size);
	if (!entry) {
		return NULL;
	}

	entry->parent = parent;

	if (parent) {
		entry->depth = parent->depth + 1;
		entry->nameoff = parent->nameoff + parent->namelen;
	} else {
		entry->depth = 0;
		entry->nameoff = 0;
	}

	entry->refcount = 1;
	entry->fd = -1;

	memcpy(entry->name, name, namelen);
	if (needs_slash) {
		entry->name[namelen++] = '/';
	}
	entry->namelen = namelen;

	while (parent) {
		dircache_entry_incref(cache, parent);
		parent = parent->parent;
	}

	return entry;
}

/**
 * Get the full path do a dircache_entry.
 *
 * @param entry
 *         The entry to look up.
 * @param[out] path
 *         Will hold the full path to the entry, with a trailing '/'.
 */
static int dircache_entry_path(const struct dircache_entry *entry, struct dynstr *path) {
	size_t namelen = entry->namelen;
	size_t pathlen = entry->nameoff + namelen;

	if (dynstr_grow(path, pathlen) != 0) {
		return -1;
	}
	path->length = pathlen;

	// Build the path backwards
	path->str[pathlen] = '\0';

	do {
		char *segment = path->str + entry->nameoff;
		namelen = entry->namelen;
		memcpy(segment, entry->name, namelen);
		entry = entry->parent;
	} while (entry);

	return 0;
}

/**
 * Get the appropriate (fd, path) pair for the *at() family of functions.
 *
 * @param cache
 *         The cache containing the entry.
 * @param entry
 *         The entry being accessed.
 * @param[out] at_fd
 *         Will hold the appropriate file descriptor to use.
 * @param[in,out] at_path
 *         Will hold the appropriate path to use.
 * @return The closest open ancestor entry.
 */
static struct dircache_entry *dircache_entry_base(struct dircache *cache, struct dircache_entry *entry, int *at_fd, const char **at_path) {
	struct dircache_entry *base = entry;

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
 * @param save
 *         A dircache_entry that must be preserved.
 */
static bool dircache_should_retry(struct dircache *cache, const struct dircache_entry *save) {
	if (errno == EMFILE && cache->size > 1) {
		// Too many open files, shrink the cache
		struct dircache_entry *entry = cache->heap[0];
		if (entry == save) {
			entry = cache->heap[1];
		}

		dircache_pop(cache, entry);
		cache->capacity = cache->size;
		return true;
	} else {
		return false;
	}
}

static size_t misses = 0;
static size_t total = 0;

/**
 * Open a dircache_entry.
 *
 * @param cache
 *         The cache containing the entry.
 * @param entry
 *         The entry to open.
 * @param path
 *         The full path to the entry (see dircache_entry_path()).
 * @return
 *         The opened DIR *, or NULL on error.
 */
static DIR *dircache_entry_open(struct dircache *cache, struct dircache_entry *entry, const char *path) {
	assert(entry->fd < 0);

	if (cache->size == cache->capacity) {
		dircache_pop(cache, cache->heap[0]);
	}

	int at_fd = AT_FDCWD;
	const char *at_path = path;
	struct dircache_entry *base = dircache_entry_base(cache, entry, &at_fd, &at_path);

	++total;
	struct dircache_entry *asdf = entry;
	do {
		++misses;
		asdf = asdf->parent;
	} while (asdf != base);

	int flags = O_DIRECTORY;
	int fd = openat(at_fd, at_path, flags);

	if (fd < 0 && dircache_should_retry(cache, base)) {
		fd = openat(at_fd, at_path, flags);
	}
	if (fd < 0) {
		return NULL;
	}

	entry->fd = fd;
	dircache_push(cache, entry);

	// Now we dup() the fd and pass it to fdopendir().  This way we can
	// close the DIR* as soon as we're done with it, reducing the memory
	// footprint significantly, while keeping the fd around for future
	// openat() calls.

	fd = dup(entry->fd);

	if (fd < 0 && dircache_should_retry(cache, entry)) {
		fd = dup(entry->fd);
	}
	if (fd < 0) {
		return NULL;
	}

	DIR *dir = fdopendir(fd);
	if (!dir) {
		close(fd);
	}
	return dir;
}

/** Free a dircache_entry. */
static void dircache_entry_free(struct dircache *cache, struct dircache_entry *entry) {
	if (entry) {
		assert(entry->refcount == 0);

		if (entry->fd >= 0) {
			dircache_pop(cache, entry);
		}

		free(entry);
	}
}

/**
 * A queue of 'dircache_entry's to examine.
 */
struct dirqueue {
	/** The circular buffer of entries. */
	struct dircache_entry **entries;
	/** Bitmask for circular buffer indices; one less than the capacity. */
	size_t mask;
	/** The index of the front of the queue. */
	size_t front;
	/** The index of the back of the queue. */
	size_t back;
};

/** Initialize a dirqueue. */
static int dirqueue_init(struct dirqueue *queue) {
	size_t size = 256;
	queue->entries = malloc(size*sizeof(struct dircache_entry *));
	if (!queue->entries) {
		return -1;
	}

	queue->mask = size - 1;
	queue->front = 0;
	queue->back = 0;
	return 0;
}

/** Add an entry to the dirqueue. */
static int dirqueue_push(struct dirqueue *queue, struct dircache_entry *entry) {
	size_t back = queue->back;

	queue->entries[back] = entry;
	back += 1;
	back &= queue->mask;

	if (back == queue->front) {
		size_t old_size = queue->mask + 1;
		struct dircache_entry **old_entries = queue->entries;

		size_t new_size = 2*old_size;
		struct dircache_entry **new_entries = malloc(new_size*sizeof(struct dircache_entry *));
		if (!new_entries) {
			return -1;
		}

		size_t mid = old_size - back;
		memcpy(new_entries, old_entries + back, mid*sizeof(struct dircache_entry *));
		memcpy(new_entries + mid, old_entries, back*sizeof(struct dircache_entry *));
		free(old_entries);

		queue->entries = new_entries;
		queue->mask = new_size - 1;
		queue->front = 0;
		queue->back = old_size;
	} else {
		queue->back = back;
	}

	return 0;
}

/** Remove an entry from the dirqueue. */
static struct dircache_entry *dirqueue_pop(struct dirqueue *queue) {
	if (queue->front == queue->back) {
		free(queue->entries);
		return NULL;
	}

	struct dircache_entry *entry = queue->entries[queue->front];
	queue->front += 1;
	queue->front &= queue->mask;

	return entry;
}

/** Fill in ftwbuf fields with information from a struct dirent. */
static void ftwbuf_use_dirent(struct BFTW *ftwbuf, const struct dirent *de) {
#if defined(_DIRENT_HAVE_D_TYPE) || defined(DT_DIR)
	switch (de->d_type) {
	case DT_BLK:
		ftwbuf->typeflag = BFTW_BLK;
		break;
	case DT_CHR:
		ftwbuf->typeflag = BFTW_CHR;
		break;
	case DT_DIR:
		ftwbuf->typeflag = BFTW_DIR;
		break;
	case DT_FIFO:
		ftwbuf->typeflag = BFTW_FIFO;
		break;
	case DT_LNK:
		ftwbuf->typeflag = BFTW_LNK;
		break;
	case DT_REG:
		ftwbuf->typeflag = BFTW_REG;
		break;
	case DT_SOCK:
		ftwbuf->typeflag = BFTW_SOCK;
		break;
	}
#endif
}

/** Call stat() and use the results. */
static int ftwbuf_stat(struct BFTW *ftwbuf, struct stat *sb, int flags) {
	int ret = fstatat(ftwbuf->at_fd, ftwbuf->at_path, sb, flags);
	if (ret != 0) {
		return ret;
	}

	ftwbuf->statbuf = sb;

	switch (sb->st_mode & S_IFMT) {
	case S_IFBLK:
		ftwbuf->typeflag = BFTW_BLK;
		break;
	case S_IFCHR:
		ftwbuf->typeflag = BFTW_CHR;
		break;
	case S_IFDIR:
		ftwbuf->typeflag = BFTW_DIR;
		break;
	case S_IFIFO:
		ftwbuf->typeflag = BFTW_FIFO;
		break;
	case S_IFLNK:
		ftwbuf->typeflag = BFTW_LNK;
		break;
	case S_IFREG:
		ftwbuf->typeflag = BFTW_REG;
		break;
	case S_IFSOCK:
		ftwbuf->typeflag = BFTW_SOCK;
		break;
	}

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
	/** dircache_entry's are being garbage collected. */
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
	struct dircache cache;

	/** The queue of directories left to explore. */
	struct dirqueue queue;
	/** The current dircache entry. */
	struct dircache_entry *current;
	/** The current traversal status. */
	enum bftw_status status;

	/** The current path being explored. */
	struct dynstr path;

	/** Extra data about the current file. */
	struct BFTW ftwbuf;
	/** stat() buffer for the current file. */
	struct stat statbuf;
};

/**
 * Initialize the bftw() state.
 */
static int bftw_state_init(struct bftw_state *state, bftw_fn *fn, int nopenfd, int flags, void *ptr) {
	state->fn = fn;
	state->flags = flags;
	state->ptr = ptr;

	state->error = 0;

	if (nopenfd < 2) {
		errno = EMFILE;
		return -1;
	}

	// -1 to account for dup()
	if (dircache_init(&state->cache, nopenfd - 1) != 0) {
		return -1;
	}

	if (dirqueue_init(&state->queue) != 0) {
		dircache_free(&state->cache);
		return -1;
	}

	state->current = NULL;
	state->status = BFTW_CURRENT;

	dynstr_init(&state->path);

	return 0;
}

/**
 * Concatenate a subpath to the current path.
 */
static int bftw_path_concat(struct bftw_state *state, const char *subpath) {
	size_t nameoff = 0;

	struct dircache_entry *current = state->current;
	if (current) {
		nameoff = current->nameoff + current->namelen;
	}

	state->status = BFTW_CHILD;

	return dynstr_concat(&state->path, nameoff, subpath);
}

/**
 * Record an error.
 */
static void bftw_set_error(struct bftw_state *state, int error) {
	state->error = error;
	state->ftwbuf.error = error;
	state->ftwbuf.typeflag = BFTW_ERROR;
}

/**
 * Figure out the name offset in a path.
 */
static size_t basename_offset(const char *path) {
	size_t i;

	// Strip trailing slashes
	for (i = strlen(path); i > 0 && path[i - 1] == '/'; --i);

	// Find the beginning of the name
	for (; i > 0 && path[i - 1] != '/'; --i);

	return i;
}

/**
 * Initialize the buffers with data about the current path.
 */
static void bftw_init_buffers(struct bftw_state *state, const struct dirent *de) {
	struct BFTW *ftwbuf = &state->ftwbuf;
	ftwbuf->path = state->path.str;
	ftwbuf->error = 0;
	ftwbuf->visit = (state->status == BFTW_GC ? BFTW_POST : BFTW_PRE);
	ftwbuf->statbuf = NULL;
	ftwbuf->at_fd = AT_FDCWD;
	ftwbuf->at_path = ftwbuf->path;

	struct dircache_entry *current = state->current;
	if (current) {
		ftwbuf->nameoff = current->nameoff;
		ftwbuf->depth = current->depth;

		if (state->status == BFTW_CHILD) {
			ftwbuf->nameoff += current->namelen;
			++ftwbuf->depth;
		}

		dircache_entry_base(&state->cache, current, &ftwbuf->at_fd, &ftwbuf->at_path);
	} else {
		ftwbuf->nameoff = basename_offset(ftwbuf->path);
		ftwbuf->depth = 0;
	}

	if (de) {
		ftwbuf_use_dirent(ftwbuf, de);
	} else if (state->status != BFTW_CHILD) {
		ftwbuf->typeflag = BFTW_DIR;
	} else {
		ftwbuf->typeflag = BFTW_UNKNOWN;
	}

	bool follow = state->flags & (current ? BFTW_FOLLOW_NONROOT : BFTW_FOLLOW_ROOT);
	ftwbuf->at_flags = follow ? 0 : AT_SYMLINK_NOFOLLOW;

	bool detect_cycles = (state->flags & BFTW_DETECT_CYCLES)
		&& state->status == BFTW_CHILD;

	bool mount = state->flags & BFTW_MOUNT;

	if ((state->flags & BFTW_STAT)
	    || ftwbuf->typeflag == BFTW_UNKNOWN
	    || (ftwbuf->typeflag == BFTW_LNK && follow)
	    || (ftwbuf->typeflag == BFTW_DIR && (detect_cycles || mount))) {
		int ret = ftwbuf_stat(ftwbuf, &state->statbuf, ftwbuf->at_flags);
		if (ret != 0 && follow && errno == ENOENT) {
			// Could be a broken symlink, retry without following
			ret = ftwbuf_stat(ftwbuf, &state->statbuf, AT_SYMLINK_NOFOLLOW);
		}

		if (ret != 0) {
			bftw_set_error(state, errno);
			return;
		}

		if (ftwbuf->typeflag == BFTW_DIR && detect_cycles) {
			dev_t dev = ftwbuf->statbuf->st_dev;
			ino_t ino = ftwbuf->statbuf->st_ino;
			for (const struct dircache_entry *entry = current; entry; entry = entry->parent) {
				if (dev == entry->dev && ino == entry->ino) {
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
 * Invoke the callback on the given path.
 */
static int bftw_handle_path(struct bftw_state *state) {
	// Never give the callback BFTW_ERROR unless BFTW_RECOVER is specified
	if (state->ftwbuf.typeflag == BFTW_ERROR && !(state->flags & BFTW_RECOVER)) {
		return BFTW_FAIL;
	}

	enum bftw_action action = state->fn(&state->ftwbuf, state->ptr);
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
 * Add a new entry to the cache.
 */
static struct dircache_entry *bftw_add(struct bftw_state *state, const char *name) {
	struct dircache_entry *entry = dircache_add(&state->cache, state->current, name);
	if (!entry) {
		return NULL;
	}

	if (state->flags & (BFTW_DETECT_CYCLES | BFTW_MOUNT)) {
		const struct stat *statbuf = state->ftwbuf.statbuf;
		if (statbuf) {
			entry->dev = statbuf->st_dev;
			entry->ino = statbuf->st_ino;
		}
	}

	return entry;
}

/**
 * Push a new entry onto the queue.
 */
static int bftw_push(struct bftw_state *state, const char *name) {
	struct dircache_entry *entry = bftw_add(state, name);
	if (!entry) {
		return -1;
	}

	return dirqueue_push(&state->queue, entry);
}

/**
 * Pop an entry off the queue.
 */
static int bftw_pop(struct bftw_state *state, bool invoke_callback) {
	int ret = BFTW_CONTINUE;
	struct dircache_entry *entry = state->current;

	if (!(state->flags & BFTW_DEPTH)) {
		invoke_callback = false;
	}

	if (entry && invoke_callback) {
		if (dircache_entry_path(entry, &state->path) != 0) {
			ret = BFTW_FAIL;
			invoke_callback = false;
		}
	}

	state->status = BFTW_GC;

	while (entry) {
		struct dircache_entry *current = entry;
		entry = entry->parent;

		dircache_entry_decref(&state->cache, current);
		if (current->refcount > 0) {
			continue;
		}

		if (invoke_callback) {
			size_t offset = current->nameoff + current->namelen;
			state->path.str[offset] = '\0';
			if (current->namelen > 1) {
				// Trim the trailing slash
				state->path.str[offset - 1] = '\0';
			}

			state->current = current;
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

		dircache_entry_free(&state->cache, current);
	}

	state->current = dirqueue_pop(&state->queue);
	state->status = BFTW_CURRENT;

	return ret;
}

/**
 * Dispose of the bftw() state.
 */
static void bftw_state_free(struct bftw_state *state) {
	while (state->current) {
		bftw_pop(state, false);
	}

	dircache_free(&state->cache);

	dynstr_free(&state->path);
}

int bftw(const char *path, bftw_fn *fn, int nopenfd, enum bftw_flags flags, void *ptr) {
	int ret = -1;

	struct bftw_state state;
	if (bftw_state_init(&state, fn, nopenfd, flags, ptr) != 0) {
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
		if (dircache_entry_path(state.current, &state.path) != 0) {
			goto fail;
		}

		DIR *dir = dircache_entry_open(&state.cache, state.current, state.path.str);
		if (!dir) {
			int error = errno;

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
		}

		struct dirent *de;
		while ((de = readdir(dir)) != NULL) {
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
				closedir(dir);
				goto next;

			case BFTW_SKIP_SUBTREE:
				continue;

			case BFTW_STOP:
				closedir(dir);
				goto done;

			case BFTW_FAIL:
				closedir(dir);
				goto fail;
			}

			if (state.ftwbuf.typeflag == BFTW_DIR) {
				const struct stat *statbuf = state.ftwbuf.statbuf;
				if ((flags & BFTW_MOUNT)
				    && statbuf
				    && statbuf->st_dev != state.current->dev) {
					continue;
				}

				if (bftw_push(&state, de->d_name) != 0) {
					closedir(dir);
					goto fail;
				}
			}
		}

		closedir(dir);

	next:
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
	} while (state.current);

done:
	if (state.error == 0) {
		ret = 0;
	}

fail:
	if (state.error == 0) {
		state.error = errno;
	}

	bftw_state_free(&state);

	errno = state.error;
	return ret;
}
