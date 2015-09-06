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
 * much as possible.  The 'dircache' attempts to accomplish this by storing a
 * hierarchy of 'dircache_entry's, along with an LRU list of recently accessed
 * entries.  Every entry in the LRU list has an open DIR *; to open an entry, we
 * traverse its chain of parents, hoping to find an open one.  The size of the
 * LRU list is limited, because so are the available file descriptors.
 *
 * The 'dirqueue' is a simple FIFO of 'dircache_entry's left to explore.
 */

#include "bftw.h"
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

/**
 * Simple dynamically-sized string type.
 */
typedef struct {
	char *str;
	size_t length;
	size_t capacity;
} dynstr;

/** Initialize a dynstr. */
static void dynstr_init(dynstr *dstr) {
	dstr->str = NULL;
	dstr->length = 0;
	dstr->capacity = 0;
}

/** Grow a dynstr to the given capacity if necessary. */
static int dynstr_grow(dynstr *dstr, size_t length) {
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
static int dynstr_concat(dynstr *dstr, size_t pos, const char *more) {
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
static void dynstr_free(dynstr *dstr) {
	free(dstr->str);
}

/**
 * A single entry in the dircache.
 */
typedef struct dircache_entry dircache_entry;

struct dircache_entry {
	/** The parent entry, if any. */
	dircache_entry *parent;
	/** This directory's depth in the walk. */
	size_t depth;

	/** Previous node in the LRU list. */
	dircache_entry *lru_prev;
	/** Next node in the LRU list. */
	dircache_entry *lru_next;

	/** The DIR pointer, if open. */
	DIR *dir;

	/** Reference count. */
	size_t refcount;

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
typedef struct {
	/** Most recently used entry. */
	dircache_entry *lru_head;
	/** Least recently used entry. */
	dircache_entry *lru_tail;
	/** Remaining LRU list capacity. */
	size_t lru_remaining;
} dircache;

/** Initialize a dircache. */
static void dircache_init(dircache *cache, size_t lru_size) {
	assert(lru_size > 0);
	cache->lru_head = cache->lru_tail = NULL;
	cache->lru_remaining = lru_size;
}

/** Add an entry to the dircache. */
static dircache_entry *dircache_add(dircache *cache, dircache_entry *parent, const char *name) {
	size_t namelen = strlen(name);
	size_t size = sizeof(dircache_entry) + namelen + 1;

	bool needs_slash = false;
	if (namelen == 0 || name[namelen - 1] != '/') {
		needs_slash = true;
		++size;
	}

	dircache_entry *entry = malloc(size);
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

	entry->lru_prev = entry->lru_next = NULL;
	entry->dir = NULL;
	entry->refcount = 1;

	memcpy(entry->name, name, namelen);
	if (needs_slash) {
		entry->name[namelen++] = '/';
	}
	entry->name[namelen] = '\0';
	entry->namelen = namelen;

	while (parent) {
		++parent->refcount;
		parent = parent->parent;
	}

	return entry;
}

/** Add an entry to the head of the LRU list. */
static void dircache_lru_add(dircache *cache, dircache_entry *entry) {
	assert(entry->dir);
	assert(entry->lru_prev == NULL);
	assert(entry->lru_next == NULL);

	entry->lru_next = cache->lru_head;
	cache->lru_head = entry;

	if (entry->lru_next) {
		entry->lru_next->lru_prev = entry;
	}

	if (!cache->lru_tail) {
		cache->lru_tail = entry;
	}

	--cache->lru_remaining;
}

/** Remove an entry from the LRU list. */
static void dircache_lru_remove(dircache *cache, dircache_entry *entry) {
	if (entry->lru_prev) {
		assert(cache->lru_head != entry);
		entry->lru_prev->lru_next = entry->lru_next;
	} else {
		assert(cache->lru_head == entry);
		cache->lru_head = entry->lru_next;
	}

	if (entry->lru_next) {
		assert(cache->lru_tail != entry);
		entry->lru_next->lru_prev = entry->lru_prev;
	} else {
		assert(cache->lru_tail == entry);
		cache->lru_tail = entry->lru_prev;
	}

	entry->lru_prev = entry->lru_next = NULL;

	++cache->lru_remaining;
}

/** Close a dircache_entry and remove it from the LRU list. */
static void dircache_entry_close(dircache *cache, dircache_entry *entry) {
	dircache_lru_remove(cache, entry);
	closedir(entry->dir);
	entry->dir = NULL;
}

/** POSIX doesn't have this?! */
static DIR *opendirat(int fd, const char *name) {
	int dfd = openat(fd, name, O_DIRECTORY);
	if (dfd < 0) {
		return NULL;
	}

	return fdopendir(dfd);
}

/**
 * Get the full path do a dircache_entry.
 *
 * @param entry
 *         The entry to look up.
 * @param[out] path
 *         Will hold the full path to the entry, with a trailing '/'.
 */
static int dircache_entry_path(dircache_entry *entry, dynstr *path) {
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
static DIR *dircache_entry_open(dircache *cache, dircache_entry *entry, const char *path) {
	assert(!entry->dir);

	if (cache->lru_remaining == 0) {
		dircache_entry_close(cache, cache->lru_tail);
	}

	size_t nameoff;
	dircache_entry *base = entry;
	do {
		nameoff = base->nameoff;
		base = base->parent;
	} while (base && !base->dir);

	int fd = AT_FDCWD;
	if (base) {
		dircache_lru_remove(cache, base);
		dircache_lru_add(cache, base);
		fd = dirfd(base->dir);
	}

	const char *relpath = path + nameoff;
	DIR *dir = opendirat(fd, relpath);

	if (!dir
	    && errno == EMFILE
	    && cache->lru_tail
	    && cache->lru_tail != base) {
		// Too many open files, shrink the LRU cache
		dircache_entry_close(cache, cache->lru_tail);
		--cache->lru_remaining;
		dir = opendirat(fd, relpath);
	}

	if (dir) {
		entry->dir = dir;
		dircache_lru_add(cache, entry);
	}

	return dir;
}

/** Free a dircache_entry. */
static void dircache_entry_free(dircache *cache, dircache_entry *entry) {
	while (entry) {
		dircache_entry *saved = entry;
		entry = entry->parent;

		if (--saved->refcount == 0) {
			if (saved->dir) {
				dircache_entry_close(cache, saved);
			}
			free(saved);
		}
	}
}

/** The size of a dirqueue block. */
#define DIRQUEUE_BLOCK_SIZE 1023

/**
 * A single block in the dirqueue chain.
 */
typedef struct dirqueue_block dirqueue_block;

struct dirqueue_block {
	/** The next block in the chain. */
	dirqueue_block *next;
	/** The elements in the queue. */
	dircache_entry *entries[DIRQUEUE_BLOCK_SIZE];
};

/**
 * A queue of 'dircache_entry's to examine.
 */
typedef struct {
	/** The first block. */
	dirqueue_block *head;
	/** The last block. */
	dirqueue_block *tail;
	/** The index in 'head' of the next entry to read. */
	size_t front;
	/** The index in 'tail' of the next entry to write. */
	size_t back;
} dirqueue;

/** Initialize a dirqueue. */
static void dirqueue_init(dirqueue *queue) {
	queue->head = queue->tail = NULL;
	queue->front = 0;
	queue->back = DIRQUEUE_BLOCK_SIZE;
}

/** Add an entry to the dirqueue. */
static int dirqueue_push(dirqueue *queue, dircache_entry *entry) {
	if (queue->back == DIRQUEUE_BLOCK_SIZE) {
		dirqueue_block *block = malloc(sizeof(dirqueue_block));
		if (!block) {
			return -1;
		}

		block->next = NULL;

		if (queue->tail) {
			queue->tail->next = block;
		}
		queue->tail = block;

		if (!queue->head) {
			queue->head = block;
		}

		queue->back = 0;
	}

	queue->tail->entries[queue->back++] = entry;
	return 0;
}

/** Remove an entry from the dirqueue. */
static dircache_entry *dirqueue_pop(dirqueue *queue) {
	if (!queue->head) {
		return NULL;
	}

	if (queue->head == queue->tail && queue->front == queue->back) {
		free(queue->head);
		dirqueue_init(queue);
		return NULL;
	}

	dirqueue_block *head = queue->head;
	dircache_entry *entry = head->entries[queue->front];
	if (++queue->front == DIRQUEUE_BLOCK_SIZE) {
		queue->head = head->next;
		queue->front = 0;
		free(head);
	}
	return entry;
}

static void ftwbuf_init(struct BFTW *ftwbuf, int base, int level) {
	ftwbuf->statbuf = NULL;
	ftwbuf->typeflag = BFTW_UNKNOWN;
	ftwbuf->base = base;
	ftwbuf->level = level;
	ftwbuf->error = 0;
}

static void ftwbuf_set_error(struct BFTW *ftwbuf, int error) {
	ftwbuf->typeflag = BFTW_ERROR;
	ftwbuf->error = error;
}

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

static int ftwbuf_stat(struct BFTW *ftwbuf, struct stat *sb, int fd, const char *path) {
	int ret = fstatat(fd, path, sb, AT_SYMLINK_NOFOLLOW);
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
 * Holds the current state of the bftw() traversal.
 */
typedef struct {
	/** bftw() callback. */
	bftw_fn *fn;
	/** bftw() flags. */
	int flags;
	/** bftw() callback data. */
	void *ptr;

	/** The appropriate errno value, if any. */
	int error;

	/** The cache of open directories. */
	dircache cache;
	/** The current dircache entry. */
	dircache_entry *current;

	/** The queue of directories left to explore. */
	dirqueue queue;
	/** The current path being explored. */
	dynstr path;

	/** Extra data about the current file. */
	struct BFTW ftwbuf;
	/** stat() buffer for the current file. */
	struct stat statbuf;
} bftw_state;

/**
 * Initialize the bftw() state.
 */
static void bftw_state_init(bftw_state *state, bftw_fn *fn, int nopenfd, int flags, void *ptr) {
	state->fn = fn;
	state->flags = flags;
	state->ptr = ptr;

	state->error = 0;

	dircache_init(&state->cache, nopenfd);
	state->current = NULL;

	dirqueue_init(&state->queue);

	dynstr_init(&state->path);
}

/**
 * Set the current path.
 */
static int bftw_set_path(bftw_state *state, const struct dirent *de, const char *name) {
	size_t base = 0;
	size_t level = 0;
	int fd = AT_FDCWD;

	dircache_entry *current = state->current;
	if (current) {
		base = current->nameoff + current->namelen;
		level = current->depth + 1;
		fd = dirfd(current->dir);
	}

	if (dynstr_concat(&state->path, base, name) != 0) {
		return -1;
	}

	ftwbuf_init(&state->ftwbuf, base, level);

	if (de) {
		ftwbuf_use_dirent(&state->ftwbuf, de);
	}

	if ((state->flags & BFTW_STAT) || state->ftwbuf.typeflag == BFTW_UNKNOWN) {
		if (ftwbuf_stat(&state->ftwbuf, &state->statbuf, fd, name) != 0) {
			state->error = errno;
			ftwbuf_set_error(&state->ftwbuf, state->error);
		}
	}

	return 0;
}

/** internal action: Abort the traversal. */
#define BFTW_FAIL (-1)

/**
 * Invoke the callback on the given path.
 */
static int bftw_handle_path(bftw_state *state) {
	// Never give the callback BFTW_ERROR unless BFTW_RECOVER is specified
	if (state->ftwbuf.typeflag == BFTW_ERROR && !(state->flags & BFTW_RECOVER)) {
		return BFTW_FAIL;
	}

	int action = state->fn(state->path.str, &state->ftwbuf, state->ptr);
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
 * Push a new entry onto the queue.
 */
static int bftw_push(bftw_state *state, const char *name) {
	dircache_entry *entry = dircache_add(&state->cache, state->current, name);
	if (!entry) {
		return -1;
	}

	return dirqueue_push(&state->queue, entry);
}

/**
 * Pop an entry off the queue.
 */
static void bftw_pop(bftw_state *state) {
	dircache_entry_free(&state->cache, state->current);
	state->current = dirqueue_pop(&state->queue);
}

/**
 * Dispose of the bftw() state.
 */
static void bftw_state_free(bftw_state *state) {
	while (state->current) {
		bftw_pop(state);
	}

	dynstr_free(&state->path);
}

int bftw(const char *path, bftw_fn *fn, int nopenfd, int flags, void *ptr) {
	int ret = -1;

	bftw_state state;
	bftw_state_init(&state, fn, nopenfd, flags, ptr);

	// Handle 'path' itself first

	if (bftw_set_path(&state, NULL, path) != 0) {
		goto fail;
	}

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

	state.current = dircache_add(&state.cache, NULL, path);
	if (!state.current) {
		goto fail;
	}

	do {
		if (dircache_entry_path(state.current, &state.path) != 0) {
			goto fail;
		}

		DIR *dir = dircache_entry_open(&state.cache, state.current, state.path.str);
		if (!dir) {
			state.error = errno;

			ftwbuf_init(&state.ftwbuf, state.current->nameoff, state.current->depth);
			ftwbuf_set_error(&state.ftwbuf, state.error);

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

			if (bftw_set_path(&state, de, de->d_name) != 0) {
				goto fail;
			}

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
				if (bftw_push(&state, de->d_name) != 0) {
					goto fail;
				}
			}
		}

	next:
		bftw_pop(&state);
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
