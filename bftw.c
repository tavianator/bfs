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

	/** Previous node in the LRU list. */
	dircache_entry *lru_prev;
	/** Next node in the LRU list. */
	dircache_entry *lru_next;

	/** The DIR pointer, if open. */
	DIR *dir;

	/** Reference count. */
	size_t refcount;

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
static dircache_entry *dircache_add(dircache *cache, dircache_entry *parent, const char *path) {
	size_t pathsize = strlen(path) + 1;
	dircache_entry *entry = malloc(sizeof(dircache_entry) + pathsize);
	if (entry) {
		entry->parent = parent;
		entry->lru_prev = entry->lru_next = NULL;
		entry->dir = NULL;
		entry->refcount = 1;
		entry->namelen = pathsize - 1;
		memcpy(entry->name, path, pathsize);

		while (parent) {
			++parent->refcount;
			parent = parent->parent;
		}
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
 * Open a dircache_entry.
 *
 * @param cache
 *         The cache containing the entry.
 * @param entry
 *         The entry to open.
 * @param[out] path
 *         Will hold the full path to the entry, with a trailing '/'.
 * @return
 *         The opened DIR *, or NULL on error.
 */
static DIR *dircache_entry_open(dircache *cache, dircache_entry *entry, dynstr *path) {
	assert(!entry->dir);

	if (cache->lru_remaining == 0) {
		dircache_entry_close(cache, cache->lru_tail);
	}

	// First, reserve enough space for the path
	size_t pathlen = 0;

	dircache_entry *parent = entry;
	do {
		size_t namelen = parent->namelen;
		pathlen += namelen;

		if (namelen > 0 && parent->name[namelen - 1] != '/') {
			++pathlen;
		}

		parent = parent->parent;
	} while (parent);

	if (dynstr_grow(path, pathlen) != 0) {
		return NULL;
	}
	path->length = pathlen;

	// Now, build the path backwards while looking for a parent
	char *segment = path->str + pathlen;
	*segment = '\0';

	int fd = AT_FDCWD;
	const char *relpath = path->str;

	parent = entry;
	while (true) {
		size_t namelen = parent->namelen;
		bool needs_slash = namelen > 0 && parent->name[namelen - 1] != '/';

		segment -= namelen;
		if (needs_slash) {
			segment -= 1;
		}

		memcpy(segment, parent->name, namelen);

		if (needs_slash) {
			segment[namelen] = '/';
		}

		parent = parent->parent;
		if (!parent) {
			break;
		}

		if (parent->dir && fd == AT_FDCWD) {
			dircache_lru_remove(cache, parent);
			dircache_lru_add(cache, parent);
			fd = dirfd(parent->dir);
			relpath = segment;
		}
	}

	DIR *dir = opendirat(fd, relpath);
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

int bftw(const char *dirpath, bftw_fn *fn, int nopenfd, int flags, void *ptr) {
	int ret = -1, err;

	dircache cache;
	dircache_init(&cache, nopenfd);

	dirqueue queue;
	dirqueue_init(&queue);

	dynstr path;
	dynstr_init(&path);

	dircache_entry *current = dircache_add(&cache, NULL, dirpath);
	if (!current) {
		goto done;
	}

	do {
		DIR *dir = dircache_entry_open(&cache, current, &path);
		if (!dir) {
			goto done;
		}

		size_t pathlen = path.length;

		struct dirent *de;
		while ((de = readdir(dir)) != NULL) {
			if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) {
				continue;
			}

			if (dynstr_concat(&path, pathlen, de->d_name) != 0) {
				goto done;
			}

			int typeflag = BFTW_UNKNOWN;

#if defined(_DIRENT_HAVE_D_TYPE) || defined(DT_DIR)
			switch (de->d_type) {
			case DT_DIR:
				typeflag = BFTW_D;
				break;
			case DT_REG:
				typeflag = BFTW_R;
				break;
			case DT_LNK:
				typeflag = BFTW_SL;
				break;
			}
#endif

			struct stat sb;
			struct stat *sp = NULL;

			if ((flags & BFTW_STAT) || typeflag == BFTW_UNKNOWN) {
				if (fstatat(dirfd(dir), de->d_name, &sb, AT_SYMLINK_NOFOLLOW) == 0) {
					sp = &sb;

					switch (sb.st_mode & S_IFMT) {
					case S_IFDIR:
						typeflag = BFTW_D;
						break;
					case S_IFREG:
						typeflag = BFTW_R;
						break;
					case S_IFLNK:
						typeflag = BFTW_SL;
						break;
					}
				}
			}

			int action = fn(path.str, sp, typeflag, ptr);

			switch (action) {
			case BFTW_CONTINUE:
				if (typeflag != BFTW_D) {
					break;
				}

				dircache_entry *next = dircache_add(&cache, current, de->d_name);
				if (!next) {
					goto done;
				}

				if (dirqueue_push(&queue, next) != 0) {
					goto done;
				}
				break;

			case BFTW_SKIP_SIBLINGS:
				goto next;

			case BFTW_SKIP_SUBTREE:
				break;

			case BFTW_STOP:
				ret = 0;
				goto done;

			default:
				errno = EINVAL;
				goto done;
			}
		}

	next:
		dircache_entry_free(&cache, current);
		current = dirqueue_pop(&queue);
	} while (current);

	ret = 0;
done:
	err = errno;

	while (current) {
		dircache_entry_free(&cache, current);
		current = dirqueue_pop(&queue);
	}

	dynstr_free(&path);

	errno = err;
	return ret;
}
