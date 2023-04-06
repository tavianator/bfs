// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include "ioq.h"
#include "dir.h"
#include "list.h"
#include "lock.h"
#include "sanity.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>

/**
 * An I/O queue request.
 */
struct ioq_req {
	/** Base file descriptor for openat(). */
	int dfd;
	/** Relative path to dfd. */
	const char *path;

	/** Arbitrary user data. */
	void *ptr;
};

/**
 * An I/O queue command.
 */
struct ioq_cmd {
	union {
		struct ioq_req req;
		struct ioq_res res;
	};

	struct ioq_cmd *next;
};

/**
 * An MPMC queue of I/O commands.
 */
struct ioqq {
	pthread_mutex_t mutex;
	pthread_cond_t cond;

	bool stop;

	struct ioq_cmd *head;
	struct ioq_cmd **tail;
};

static struct ioqq *ioqq_create(void) {
	struct ioqq *ioqq = malloc(sizeof(*ioqq));
	if (!ioqq) {
		goto fail;
	}

	if (mutex_init(&ioqq->mutex, NULL) != 0) {
		goto fail_free;
	}

	if (cond_init(&ioqq->cond, NULL) != 0) {
		goto fail_mutex;
	}

	ioqq->stop = false;
	SLIST_INIT(ioqq);
	return ioqq;

fail_mutex:
	mutex_destroy(&ioqq->mutex);
fail_free:
	free(ioqq);
fail:
	return NULL;
}

/** Push a command onto the queue. */
static void ioqq_push(struct ioqq *ioqq, struct ioq_cmd *cmd) {
	mutex_lock(&ioqq->mutex);
	SLIST_APPEND(ioqq, cmd);
	mutex_unlock(&ioqq->mutex);
	cond_signal(&ioqq->cond);
}

/** Pop a command from a queue. */
static struct ioq_cmd *ioqq_pop(struct ioqq *ioqq) {
	mutex_lock(&ioqq->mutex);

	while (!ioqq->stop && !ioqq->head) {
		cond_wait(&ioqq->cond, &ioqq->mutex);
	}

	struct ioq_cmd *cmd = SLIST_POP(ioqq);
	mutex_unlock(&ioqq->mutex);
	return cmd;
}

/** Pop a command from a queue without blocking. */
static struct ioq_cmd *ioqq_trypop(struct ioqq *ioqq) {
	if (!mutex_trylock(&ioqq->mutex)) {
		return NULL;
	}

	struct ioq_cmd *cmd = SLIST_POP(ioqq);
	mutex_unlock(&ioqq->mutex);
	return cmd;
}

/** Stop a queue, waking up any waiters. */
static void ioqq_stop(struct ioqq *ioqq) {
	mutex_lock(&ioqq->mutex);
	ioqq->stop = true;
	mutex_unlock(&ioqq->mutex);
	cond_broadcast(&ioqq->cond);
}

static void ioqq_destroy(struct ioqq *ioqq) {
	if (ioqq) {
		cond_destroy(&ioqq->cond);
		mutex_destroy(&ioqq->mutex);
		free(ioqq);
	}
}

struct ioq {
	/** The depth of the queue. */
	size_t depth;
	/** The current size of the queue. */
	size_t size;

	/** Pending I/O requests. */
	struct ioqq *pending;
	/** Ready I/O responses. */
	struct ioqq *ready;

	/** The number of background threads. */
	size_t nthreads;
	/** The background threads themselves. */
	pthread_t *threads;
};

/** Background thread entry point. */
static void *ioq_work(void *ptr) {
	struct ioq *ioq = ptr;

	while (true) {
		struct ioq_cmd *cmd = ioqq_pop(ioq->pending);
		if (!cmd) {
			break;
		}

		struct ioq_req req = cmd->req;
		sanitize_uninit(cmd);

		struct ioq_res *res = &cmd->res;
		res->dir = bfs_opendir(req.dfd, req.path);
		res->error = errno;
		ioqq_push(ioq->ready, cmd);
	}

	return NULL;
}

struct ioq *ioq_create(size_t depth, size_t threads) {
	struct ioq *ioq = malloc(sizeof(*ioq));
	if (!ioq) {
		goto fail;
	}

	ioq->depth = depth;
	ioq->size = 0;
	ioq->pending = NULL;
	ioq->ready = NULL;
	ioq->nthreads = 0;

	ioq->pending = ioqq_create();
	if (!ioq->pending) {
		goto fail;
	}

	ioq->ready = ioqq_create();
	if (!ioq->ready) {
		goto fail;
	}

	ioq->threads = malloc(threads * sizeof(ioq->threads[0]));
	if (!ioq->threads) {
		goto fail;
	}

	for (size_t i = 0; i < threads; ++i) {
		errno = pthread_create(&ioq->threads[i], NULL, ioq_work, ioq);
		if (errno != 0) {
			goto fail;
		}
		++ioq->nthreads;
	}

	return ioq;

	int err;
fail:
	err = errno;
	ioq_destroy(ioq);
	errno = err;
	return NULL;
}

int ioq_opendir(struct ioq *ioq, int dfd, const char *path, void *ptr) {
	if (ioq->size >= ioq->depth) {
		return -1;
	}

	struct ioq_cmd *cmd = malloc(sizeof(*cmd));
	if (!cmd) {
		return -1;
	}

	struct ioq_req *req = &cmd->req;
	req->dfd = dfd;
	req->path = path;
	req->ptr = ptr;

	++ioq->size;
	ioqq_push(ioq->pending, cmd);
	return 0;
}

struct ioq_res *ioq_pop(struct ioq *ioq) {
	if (ioq->size == 0) {
		return NULL;
	}

	struct ioq_cmd *cmd = ioqq_pop(ioq->ready);
	if (!cmd) {
		return NULL;
	}

	--ioq->size;
	return &cmd->res;
}

struct ioq_res *ioq_trypop(struct ioq *ioq) {
	if (ioq->size == 0) {
		return NULL;
	}

	struct ioq_cmd *cmd = ioqq_trypop(ioq->ready);
	if (!cmd) {
		return NULL;
	}

	--ioq->size;
	return &cmd->res;
}

void ioq_free(struct ioq *ioq, struct ioq_res *res) {
	struct ioq_cmd *cmd = (struct ioq_cmd *)res;
	free(cmd);
}

void ioq_destroy(struct ioq *ioq) {
	if (!ioq) {
		return;
	}

	if (ioq->pending) {
		ioqq_stop(ioq->pending);
	}

	for (size_t i = 0; i < ioq->nthreads; ++i) {
		if (pthread_join(ioq->threads[i], NULL) != 0) {
			abort();
		}
	}
	free(ioq->threads);

	ioqq_destroy(ioq->ready);
	ioqq_destroy(ioq->pending);

	free(ioq);
}
