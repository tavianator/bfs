// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include "xspawn.h"
#include "bfstd.h"
#include "config.h"
#include "list.h"
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#if BFS_USE_PATHS_H
#  include <paths.h>
#endif

/**
 * Types of spawn actions.
 */
enum bfs_spawn_op {
	BFS_SPAWN_CLOSE,
	BFS_SPAWN_DUP2,
	BFS_SPAWN_FCHDIR,
	BFS_SPAWN_SETRLIMIT,
};

/**
 * A spawn action.
 */
struct bfs_spawn_action {
	struct bfs_spawn_action *next;

	enum bfs_spawn_op op;
	int in_fd;
	int out_fd;
	int resource;
	struct rlimit rlimit;
};

int bfs_spawn_init(struct bfs_spawn *ctx) {
	ctx->flags = 0;
	SLIST_INIT(ctx);
	return 0;
}

int bfs_spawn_destroy(struct bfs_spawn *ctx) {
	while (ctx->head) {
		struct bfs_spawn_action *action = ctx->head;
		SLIST_POP(ctx);
		free(action);
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
	if (!action) {
		return NULL;
	}

	action->next = NULL;
	action->op = op;
	action->in_fd = -1;
	action->out_fd = -1;

	SLIST_APPEND(ctx, action);
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

int bfs_spawn_addsetrlimit(struct bfs_spawn *ctx, int resource, const struct rlimit *rl) {
	struct bfs_spawn_action *action = bfs_spawn_add(ctx, BFS_SPAWN_SETRLIMIT);
	if (action) {
		action->resource = resource;
		action->rlimit = *rl;
		return 0;
	} else {
		return -1;
	}
}

/** Actually exec() the new process. */
static void bfs_spawn_exec(const char *exe, const struct bfs_spawn *ctx, char **argv, char **envp, int pipefd[2]) {
	xclose(pipefd[0]);

	for (const struct bfs_spawn_action *action = ctx ? ctx->head : NULL; action; action = action->next) {
		// Move the error-reporting pipe out of the way if necessary...
		if (action->out_fd == pipefd[1]) {
			int fd = dup_cloexec(pipefd[1]);
			if (fd < 0) {
				goto fail;
			}
			xclose(pipefd[1]);
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
		case BFS_SPAWN_SETRLIMIT:
			if (setrlimit(action->resource, &action->rlimit) != 0) {
				goto fail;
			}
			break;
		}
	}

	execve(exe, argv, envp);

	int error;
fail:
	error = errno;

	// In case of a write error, the parent will still see that we exited
	// unsuccessfully, but won't know why
	(void) xwrite(pipefd[1], &error, sizeof(error));

	xclose(pipefd[1]);
	_Exit(127);
}

pid_t bfs_spawn(const char *exe, const struct bfs_spawn *ctx, char **argv, char **envp) {
	extern char **environ;
	if (!envp) {
		envp = environ;
	}

	enum bfs_spawn_flags flags = ctx ? ctx->flags : 0;
	char *resolved = NULL;
	if (flags & BFS_SPAWN_USEPATH) {
		exe = resolved = bfs_spawn_resolve(exe);
		if (!resolved) {
			return -1;
		}
	}

	// Use a pipe to report errors from the child
	int pipefd[2];
	if (pipe_cloexec(pipefd) != 0) {
		free(resolved);
		return -1;
	}

	pid_t pid = fork();
	if (pid < 0) {
		close_quietly(pipefd[1]);
		close_quietly(pipefd[0]);
		free(resolved);
		return -1;
	} else if (pid == 0) {
		// Child
		bfs_spawn_exec(exe, ctx, argv, envp, pipefd);
	}

	// Parent
	xclose(pipefd[1]);
	free(resolved);

	int error;
	ssize_t nbytes = xread(pipefd[0], &error, sizeof(error));
	xclose(pipefd[0]);
	if (nbytes == sizeof(error)) {
		int wstatus;
		waitpid(pid, &wstatus, 0);
		errno = error;
		return -1;
	}

	return pid;
}

char *bfs_spawn_resolve(const char *exe) {
	if (strchr(exe, '/')) {
		return strdup(exe);
	}

	const char *path = getenv("PATH");

	char *confpath = NULL;
	if (!path) {
#if defined(_CS_PATH)
		path = confpath = xconfstr(_CS_PATH);
#elif defined(_PATH_DEFPATH)
		path = _PATH_DEFPATH;
#else
		errno = ENOENT;
#endif
	}
	if (!path) {
		return NULL;
	}

	size_t cap = 0;
	char *ret = NULL;
	while (true) {
		const char *end = strchr(path, ':');
		size_t len = end ? (size_t)(end - path) : strlen(path);

		// POSIX 8.3: "A zero-length prefix is a legacy feature that
		// indicates the current working directory."
		if (len == 0) {
			path = ".";
			len = 1;
		}

		size_t total = len + 1 + strlen(exe) + 1;
		if (cap < total) {
			char *grown = realloc(ret, total);
			if (!grown) {
				goto fail;
			}
			ret = grown;
			cap = total;
		}

		memcpy(ret, path, len);
		if (ret[len - 1] != '/') {
			ret[len++] = '/';
		}
		strcpy(ret + len, exe);

		if (xfaccessat(AT_FDCWD, ret, X_OK) == 0) {
			break;
		}

		if (!end) {
			errno = ENOENT;
			goto fail;
		}

		path = end + 1;
	}

	free(confpath);
	return ret;

fail:
	free(confpath);
	free(ret);
	return NULL;
}
