// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include "xspawn.h"
#include "alloc.h"
#include "bfstd.h"
#include "config.h"
#include "list.h"
#include <errno.h>
#include <fcntl.h>
#include <spawn.h>
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
	ctx->flags = BFS_SPAWN_USE_POSIX;
	SLIST_INIT(ctx);

	errno = posix_spawn_file_actions_init(&ctx->actions);
	if (errno != 0) {
		return -1;
	}

	errno = posix_spawnattr_init(&ctx->attr);
	if (errno != 0) {
		posix_spawn_file_actions_destroy(&ctx->actions);
		return -1;
	}

	return 0;
}

int bfs_spawn_destroy(struct bfs_spawn *ctx) {
	posix_spawnattr_destroy(&ctx->attr);
	posix_spawn_file_actions_destroy(&ctx->actions);

	for_slist (struct bfs_spawn_action, action, ctx) {
		free(action);
	}

	return 0;
}

/** Set some posix_spawnattr flags. */
attr_maybe_unused
static int bfs_spawn_addflags(struct bfs_spawn *ctx, short flags) {
	short prev;
	errno = posix_spawnattr_getflags(&ctx->attr, &prev);
	if (errno != 0) {
		return -1;
	}

	short next = prev | flags;
	if (next != prev) {
		errno = posix_spawnattr_setflags(&ctx->attr, next);
		if (errno != 0) {
			return -1;
		}
	}

	return 0;
}

/** Allocate a spawn action. */
static struct bfs_spawn_action *bfs_spawn_action(enum bfs_spawn_op op) {
	struct bfs_spawn_action *action = ALLOC(struct bfs_spawn_action);
	if (!action) {
		return NULL;
	}

	SLIST_ITEM_INIT(action);
	action->op = op;
	action->in_fd = -1;
	action->out_fd = -1;
	return action;
}

int bfs_spawn_addclose(struct bfs_spawn *ctx, int fd) {
	struct bfs_spawn_action *action = bfs_spawn_action(BFS_SPAWN_CLOSE);
	if (!action) {
		return -1;
	}

	if (ctx->flags & BFS_SPAWN_USE_POSIX) {
		errno = posix_spawn_file_actions_addclose(&ctx->actions, fd);
		if (errno != 0) {
			free(action);
			return -1;
		}
	}

	action->out_fd = fd;
	SLIST_APPEND(ctx, action);
	return 0;
}

int bfs_spawn_adddup2(struct bfs_spawn *ctx, int oldfd, int newfd) {
	struct bfs_spawn_action *action = bfs_spawn_action(BFS_SPAWN_DUP2);
	if (!action) {
		return -1;
	}

	if (ctx->flags & BFS_SPAWN_USE_POSIX) {
		errno = posix_spawn_file_actions_adddup2(&ctx->actions, oldfd, newfd);
		if (errno != 0) {
			free(action);
			return -1;
		}
	}

	action->in_fd = oldfd;
	action->out_fd = newfd;
	SLIST_APPEND(ctx, action);
	return 0;
}

int bfs_spawn_addfchdir(struct bfs_spawn *ctx, int fd) {
	struct bfs_spawn_action *action = bfs_spawn_action(BFS_SPAWN_FCHDIR);
	if (!action) {
		return -1;
	}

#ifndef BFS_HAS_POSIX_SPAWN_FCHDIR
#  define BFS_HAS_POSIX_SPAWN_FCHDIR __NetBSD__
#endif

#ifndef BFS_HAS_POSIX_SPAWN_FCHDIR_NP
#  if __GLIBC__
#    define BFS_HAS_POSIX_SPAWN_FCHDIR_NP __GLIBC_PREREQ(2, 29)
#  elif __ANDROID__
#    define BFS_HAS_POSIX_SPAWN_FCHDIR_NP (__ANDROID_API__ >= 34)
#  else
#    define BFS_HAS_POSIX_SPAWN_FCHDIR_NP (__linux__ || __FreeBSD__ || __APPLE__)
#  endif
#endif

#if BFS_HAS_POSIX_SPAWN_FCHDIR
#  define BFS_POSIX_SPAWN_FCHDIR posix_spawn_file_actions_addfchdir
#elif BFS_HAS_POSIX_SPAWN_FCHDIR_NP
#  define BFS_POSIX_SPAWN_FCHDIR posix_spawn_file_actions_addfchdir_np
#endif

#ifdef BFS_POSIX_SPAWN_FCHDIR
	if (ctx->flags & BFS_SPAWN_USE_POSIX) {
		errno = BFS_POSIX_SPAWN_FCHDIR(&ctx->actions, fd);
		if (errno != 0) {
			free(action);
			return -1;
		}
	}
#else
	ctx->flags &= ~BFS_SPAWN_USE_POSIX;
#endif

	action->in_fd = fd;
	SLIST_APPEND(ctx, action);
	return 0;
}

int bfs_spawn_setrlimit(struct bfs_spawn *ctx, int resource, const struct rlimit *rl) {
	struct bfs_spawn_action *action = bfs_spawn_action(BFS_SPAWN_SETRLIMIT);
	if (!action) {
		goto fail;
	}

#ifdef POSIX_SPAWN_SETRLIMIT
	if (bfs_spawn_addflags(ctx, POSIX_SPAWN_SETRLIMIT) != 0) {
		goto fail;
	}

	errno = posix_spawnattr_setrlimit(&ctx->attr, resource, rl);
	if (errno != 0) {
		goto fail;
	}
#else
	ctx->flags &= ~BFS_SPAWN_USE_POSIX;
#endif

	action->resource = resource;
	action->rlimit = *rl;
	SLIST_APPEND(ctx, action);
	return 0;

fail:
	free(action);
	return -1;
}

/** bfs_spawn() implementation using posix_spawn(). */
static pid_t bfs_posix_spawn(const char *exe, const struct bfs_spawn *ctx, char **argv, char **envp) {
	pid_t ret;
	errno = posix_spawn(&ret, exe, &ctx->actions, &ctx->attr, argv, envp);
	if (errno != 0) {
		return -1;
	}

	return ret;
}

/** Actually exec() the new process. */
static noreturn void bfs_spawn_exec(const char *exe, const struct bfs_spawn *ctx, char **argv, char **envp, int pipefd[2]) {
	xclose(pipefd[0]);

	for_slist (const struct bfs_spawn_action, action, ctx) {
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
	(void)xwrite(pipefd[1], &error, sizeof(error));

	xclose(pipefd[1]);
	_Exit(127);
}

/** bfs_spawn() implementation using fork()/exec(). */
static pid_t bfs_fork_spawn(const char *exe, const struct bfs_spawn *ctx, char **argv, char **envp) {
	// Use a pipe to report errors from the child
	int pipefd[2];
	if (pipe_cloexec(pipefd) != 0) {
		return -1;
	}

	pid_t pid = fork();
	if (pid < 0) {
		close_quietly(pipefd[1]);
		close_quietly(pipefd[0]);
		return -1;
	} else if (pid == 0) {
		// Child
		bfs_spawn_exec(exe, ctx, argv, envp, pipefd);
	}

	// Parent
	xclose(pipefd[1]);

	int error;
	ssize_t nbytes = xread(pipefd[0], &error, sizeof(error));
	xclose(pipefd[0]);
	if (nbytes == sizeof(error)) {
		int wstatus;
		xwaitpid(pid, &wstatus, 0);
		errno = error;
		return -1;
	}

	return pid;
}

pid_t bfs_spawn(const char *exe, const struct bfs_spawn *ctx, char **argv, char **envp) {
	// execvp()/posix_spawnp() are typically implemented with repeated
	// execv() calls for each $PATH component until one succeeds.  It's
	// faster to resolve the full path ahead of time.
	char *resolved = NULL;
	if (ctx->flags & BFS_SPAWN_USE_PATH) {
		exe = resolved = bfs_spawn_resolve(exe);
		if (!resolved) {
			return -1;
		}
	}

	extern char **environ;
	if (!envp) {
		envp = environ;
	}

	pid_t ret;
	if (ctx->flags & BFS_SPAWN_USE_POSIX) {
		ret = bfs_posix_spawn(exe, ctx, argv, envp);
	} else {
		ret = bfs_fork_spawn(exe, ctx, argv, envp);
	}

	free(resolved);
	return ret;
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
