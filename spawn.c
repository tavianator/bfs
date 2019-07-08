/****************************************************************************
 * bfs                                                                      *
 * Copyright (C) 2018-2019 Tavian Barnes <tavianator@tavianator.com>        *
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

#include "spawn.h"
#include "util.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/**
 * Types of spawn actions.
 */
enum bfs_spawn_op {
	BFS_SPAWN_CLOSE,
	BFS_SPAWN_DUP2,
	BFS_SPAWN_FCHDIR,
};

/**
 * A spawn action.
 */
struct bfs_spawn_action {
	struct bfs_spawn_action *next;

	enum bfs_spawn_op op;
	int in_fd;
	int out_fd;
};

int bfs_spawn_init(struct bfs_spawn *ctx) {
	ctx->flags = 0;
	ctx->actions = NULL;
	ctx->tail = &ctx->actions;
	return 0;
}

int bfs_spawn_destroy(struct bfs_spawn *ctx) {
	struct bfs_spawn_action *action = ctx->actions;
	while (action) {
		struct bfs_spawn_action *next = action->next;
		free(action);
		action = next;
	}
	return 0;
}

int bfs_spawn_setflags(struct bfs_spawn *ctx, enum bfs_spawn_flags flags) {
	ctx->flags = flags;
	return 0;
}

/** Add a spawn action to the chain. */
static struct bfs_spawn_action *bfs_spawn_add(struct bfs_spawn *ctx, enum bfs_spawn_op op) {
	struct bfs_spawn_action *action = malloc(sizeof(*action));
	if (action) {
		action->next = NULL;
		action->op = op;
		action->in_fd = -1;
		action->out_fd = -1;

		*ctx->tail = action;
		ctx->tail = &action->next;
	}
	return action;
}

int bfs_spawn_addclose(struct bfs_spawn *ctx, int fd) {
	if (fd < 0) {
		errno = EBADF;
		return -1;
	}

	struct bfs_spawn_action *action = bfs_spawn_add(ctx, BFS_SPAWN_CLOSE);
	if (action) {
		action->out_fd = fd;
		return 0;
	} else {
		return -1;
	}
}

int bfs_spawn_adddup2(struct bfs_spawn *ctx, int oldfd, int newfd) {
	if (oldfd < 0 || newfd < 0) {
		errno = EBADF;
		return -1;
	}

	struct bfs_spawn_action *action = bfs_spawn_add(ctx, BFS_SPAWN_DUP2);
	if (action) {
		action->in_fd = oldfd;
		action->out_fd = newfd;
		return 0;
	} else {
		return -1;
	}
}

int bfs_spawn_addfchdir(struct bfs_spawn *ctx, int fd) {
	if (fd < 0) {
		errno = EBADF;
		return -1;
	}

	struct bfs_spawn_action *action = bfs_spawn_add(ctx, BFS_SPAWN_FCHDIR);
	if (action) {
		action->in_fd = fd;
		return 0;
	} else {
		return -1;
	}
}

/** Facade for execvpe() which is non-standard. */
static int bfs_execvpe(const char *exe, char **argv, char **envp) {
#if __GLIBC__ || __linux__ || __NetBSD__ || __OpenBSD__
	return execvpe(exe, argv, envp);
#else
	extern char **environ;
	environ = envp;
	return execvp(exe, argv);
#endif
}

/** Actually exec() the new process. */
static void bfs_spawn_exec(const char *exe, const struct bfs_spawn *ctx, char **argv, char **envp, int pipefd[2]) {
	int error;
	enum bfs_spawn_flags flags = ctx ? ctx->flags : 0;
	const struct bfs_spawn_action *actions = ctx ? ctx->actions : NULL;

	close(pipefd[0]);

	for (const struct bfs_spawn_action *action = actions; action; action = action->next) {
		// Move the error-reporting pipe out of the way if necessary...
		if (action->out_fd == pipefd[1]) {
			int fd = dup_cloexec(pipefd[1]);
			if (fd < 0) {
				goto fail;
			}
			close(pipefd[1]);
			pipefd[1] = fd;
		}

		// ... and pretend the pipe doesn't exist
		if (action->in_fd == pipefd[1]) {
			errno = EBADF;
			goto fail;
		}

		switch (action->op) {
		case BFS_SPAWN_CLOSE:
			if (close(action->out_fd) != 0) {
				goto fail;
			}
			break;
		case BFS_SPAWN_DUP2:
			if (dup2(action->in_fd, action->out_fd) < 0) {
				goto fail;
			}
			break;
		case BFS_SPAWN_FCHDIR:
			if (fchdir(action->in_fd) != 0) {
				goto fail;
			}
			break;
		}
	}

	if (flags & BFS_SPAWN_USEPATH) {
		bfs_execvpe(exe, argv, envp);
	} else {
		execve(exe, argv, envp);
	}

fail:
	error = errno;
	while (write(pipefd[1], &error, sizeof(error)) < sizeof(error));
	close(pipefd[1]);
	_Exit(127);
}

pid_t bfs_spawn(const char *exe, const struct bfs_spawn *ctx, char **argv, char **envp) {
	// Use a pipe to report errors from the child
	int pipefd[2];
	if (pipe_cloexec(pipefd) != 0) {
		return -1;
	}

	int error;
	pid_t pid = fork();

	if (pid < 0) {
		error = errno;
		close(pipefd[1]);
		close(pipefd[0]);
		errno = error;
		return -1;
	} else if (pid == 0) {
		// Child
		bfs_spawn_exec(exe, ctx, argv, envp, pipefd);
	}

	// Parent
	close(pipefd[1]);

	ssize_t nbytes = read(pipefd[0], &error, sizeof(error));
	close(pipefd[0]);
	if (nbytes == sizeof(error)) {
		int wstatus;
		waitpid(pid, &wstatus, 0);
		errno = error;
		return -1;
	}

	return pid;
}
