// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include "prelude.h"
#include "xspawn.h"
#include "alloc.h"
#include "bfstd.h"
#include "diag.h"
#include "list.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <unistd.h>

#if BFS_USE_PATHS_H
#  include <paths.h>
#endif

#if _POSIX_SPAWN > 0
#  include <spawn.h>
#endif

/**
 * Types of spawn actions.
 */
enum bfs_spawn_op {
	BFS_SPAWN_OPEN,
	BFS_SPAWN_CLOSE,
	BFS_SPAWN_DUP2,
	BFS_SPAWN_FCHDIR,
	BFS_SPAWN_SETRLIMIT,
};

/**
 * A spawn action.
 */
struct bfs_spawn_action {
	/** The next action in the list. */
	struct bfs_spawn_action *next;

	/** This action's operation. */
	enum bfs_spawn_op op;
	/** The input fd (or -1). */
	int in_fd;
	/** The output fd (or -1). */
	int out_fd;

	/** Operation-specific args. */
	union {
		/** BFS_SPAWN_OPEN args. */
		struct {
			const char *path;
			int flags;
			mode_t mode;
		};

		/** BFS_SPAWN_SETRLIMIT args. */
		struct {
			int resource;
			struct rlimit rlimit;
		};
	};
};

int bfs_spawn_init(struct bfs_spawn *ctx) {
	ctx->flags = 0;
	SLIST_INIT(ctx);

#if _POSIX_SPAWN > 0
	ctx->flags |= BFS_SPAWN_USE_POSIX;

	errno = posix_spawn_file_actions_init(&ctx->actions);
	if (errno != 0) {
		return -1;
	}

	errno = posix_spawnattr_init(&ctx->attr);
	if (errno != 0) {
		posix_spawn_file_actions_destroy(&ctx->actions);
		return -1;
	}
#endif

	return 0;
}

int bfs_spawn_destroy(struct bfs_spawn *ctx) {
#if _POSIX_SPAWN > 0
	posix_spawnattr_destroy(&ctx->attr);
	posix_spawn_file_actions_destroy(&ctx->actions);
#endif

	for_slist (struct bfs_spawn_action, action, ctx) {
		free(action);
	}

	return 0;
}

#if _POSIX_SPAWN > 0
/** Set some posix_spawnattr flags. */
attr(maybe_unused)
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
#endif // _POSIX_SPAWN > 0

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

int bfs_spawn_addopen(struct bfs_spawn *ctx, int fd, const char *path, int flags, mode_t mode) {
	struct bfs_spawn_action *action = bfs_spawn_action(BFS_SPAWN_OPEN);
	if (!action) {
		return -1;
	}

#if _POSIX_SPAWN > 0
	if (ctx->flags & BFS_SPAWN_USE_POSIX) {
		errno = posix_spawn_file_actions_addopen(&ctx->actions, fd, path, flags, mode);
		if (errno != 0) {
			free(action);
			return -1;
		}
	}
#endif

	action->out_fd = fd;
	action->path = path;
	action->flags = flags;
	action->mode = mode;
	SLIST_APPEND(ctx, action);
	return 0;
}

int bfs_spawn_addclose(struct bfs_spawn *ctx, int fd) {
	struct bfs_spawn_action *action = bfs_spawn_action(BFS_SPAWN_CLOSE);
	if (!action) {
		return -1;
	}

#if _POSIX_SPAWN > 0
	if (ctx->flags & BFS_SPAWN_USE_POSIX) {
		errno = posix_spawn_file_actions_addclose(&ctx->actions, fd);
		if (errno != 0) {
			free(action);
			return -1;
		}
	}
#endif

	action->out_fd = fd;
	SLIST_APPEND(ctx, action);
	return 0;
}

int bfs_spawn_adddup2(struct bfs_spawn *ctx, int oldfd, int newfd) {
	struct bfs_spawn_action *action = bfs_spawn_action(BFS_SPAWN_DUP2);
	if (!action) {
		return -1;
	}

#if _POSIX_SPAWN > 0
	if (ctx->flags & BFS_SPAWN_USE_POSIX) {
		errno = posix_spawn_file_actions_adddup2(&ctx->actions, oldfd, newfd);
		if (errno != 0) {
			free(action);
			return -1;
		}
	}
#endif

	action->in_fd = oldfd;
	action->out_fd = newfd;
	SLIST_APPEND(ctx, action);
	return 0;
}

/**
 * https://www.austingroupbugs.net/view.php?id=1208#c4830 says:
 *
 *     ... a search of the directories passed as the environment variable
 *     PATH ..., using the working directory of the child process after all
 *     file_actions have been performed.
 *
 * but macOS and NetBSD resolve the PATH *before* file_actions (because there
 * posix_spawn() is its own syscall).
 */
#define BFS_POSIX_SPAWNP_AFTER_FCHDIR !(__APPLE__ || __NetBSD__)

int bfs_spawn_addfchdir(struct bfs_spawn *ctx, int fd) {
	struct bfs_spawn_action *action = bfs_spawn_action(BFS_SPAWN_FCHDIR);
	if (!action) {
		return -1;
	}

#if BFS_HAS_POSIX_SPAWN_ADDFCHDIR
#  define BFS_POSIX_SPAWN_ADDFCHDIR posix_spawn_file_actions_addfchdir
#elif BFS_HAS_POSIX_SPAWN_ADDFCHDIR_NP
#  define BFS_POSIX_SPAWN_ADDFCHDIR posix_spawn_file_actions_addfchdir_np
#endif

#if _POSIX_SPAWN > 0 && defined(BFS_POSIX_SPAWN_FCHDIR)
	if (ctx->flags & BFS_SPAWN_USE_POSIX) {
		errno = BFS_POSIX_SPAWN_ADDFCHDIR(&ctx->actions, fd);
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

/**
 * Context for resolving executables in the $PATH.
 */
struct bfs_resolver {
	/** The executable to spawn. */
	const char *exe;
	/** The $PATH to resolve in. */
	char *path;
	/** A buffer to hold the resolved path. */
	char *buf;
	/** The size of the buffer. */
	size_t len;
	/** Whether the executable is already resolved. */
	bool done;
	/** Whether to free(path). */
	bool free;
};

/** Free a $PATH resolution context. */
static void bfs_resolve_free(struct bfs_resolver *res) {
	if (res->free) {
		free(res->path);
	}
	free(res->buf);
}

/** Get the next component in the $PATH. */
static bool bfs_resolve_next(const char **path, const char **next, size_t *len) {
	*path = *next;
	if (!*path) {
		return false;
	}

	*next = strchr(*path, ':');
	if (*next) {
		*len = *next - *path;
		++*next;
	} else {
		*len = strlen(*path);
	}

	if (*len == 0) {
		// POSIX 8.3: "A zero-length prefix is a legacy feature that
		// indicates the current working directory."
		*path = ".";
		*len = 1;
	}

	return true;
}

/** Finish resolving an executable, potentially from the child process. */
static int bfs_resolve_late(struct bfs_resolver *res) {
	if (res->done) {
		return 0;
	}

	char *buf = res->buf;
	char *end = buf + res->len;

	const char *path;
	const char *next = res->path;
	size_t len;
	while (bfs_resolve_next(&path, &next, &len)) {
		char *cur = xstpencpy(buf, end, path, len);
		cur = xstpecpy(cur, end, "/");
		cur = xstpecpy(cur, end, res->exe);
		if (cur == end) {
			bfs_bug("PATH resolution buffer too small");
			errno = ENOMEM;
			return -1;
		}

		if (xfaccessat(AT_FDCWD, buf, X_OK) == 0) {
			res->exe = buf;
			res->done = true;
			return 0;
		}
	}

	errno = ENOENT;
	return -1;
}

/** Check if we can skip path resolution entirely. */
static bool bfs_can_skip_resolve(const struct bfs_resolver *res, const struct bfs_spawn *ctx) {
	if (ctx && !(ctx->flags & BFS_SPAWN_USE_PATH)) {
		return true;
	}

	if (strchr(res->exe, '/')) {
		return true;
	}

	return false;
}

/** Check if any $PATH components are relative. */
static bool bfs_resolve_relative(const struct bfs_resolver *res) {
	const char *path;
	const char *next = res->path;
	size_t len;
	while (bfs_resolve_next(&path, &next, &len)) {
		if (path[0] != '/') {
			return true;
		}
	}

	return false;
}

/** Check if we can resolve the executable before file actions. */
static bool bfs_can_resolve_early(const struct bfs_resolver *res, const struct bfs_spawn *ctx) {
	if (!bfs_resolve_relative(res)) {
		return true;
	}

	if (ctx) {
		for_slist (const struct bfs_spawn_action, action, ctx) {
			if (action->op == BFS_SPAWN_FCHDIR) {
				return false;
			}
		}
	}

	return true;
}

/** Get the required path resolution buffer size. */
static size_t bfs_resolve_capacity(const struct bfs_resolver *res) {
	size_t max = 0;

	const char *path;
	const char *next = res->path;
	size_t len;
	while (bfs_resolve_next(&path, &next, &len)) {
		if (len > max) {
			max = len;
		}
	}

	// path + "/" + exe + '\0'
	return max + 1 + strlen(res->exe) + 1;
}

/** Begin resolving an executable, from the parent process. */
static int bfs_resolve_early(struct bfs_resolver *res, const char *exe, const struct bfs_spawn *ctx) {
	*res = (struct bfs_resolver) {
		.exe = exe,
	};

	if (bfs_can_skip_resolve(res, ctx)) {
		res->done = true;
		return 0;
	}

	res->path = getenv("PATH");
	if (!res->path) {
#if defined(_CS_PATH)
		res->path = xconfstr(_CS_PATH);
		res->free = true;
#elif defined(_PATH_DEFPATH)
		res->path = _PATH_DEFPATH;
#else
		errno = ENOENT;
#endif
	}
	if (!res->path) {
		goto fail;
	}

	bool can_finish = bfs_can_resolve_early(res, ctx);

#if BFS_POSIX_SPAWNP_AFTER_FCHDIR
	bool use_posix = ctx && (ctx->flags & BFS_SPAWN_USE_POSIX);
	if (!can_finish && use_posix) {
		// posix_spawnp() will do the resolution, so don't bother
		// allocating a buffer
		return 0;
	}
#endif

	res->len = bfs_resolve_capacity(res);
	res->buf = malloc(res->len);
	if (!res->buf) {
		goto fail;
	}

	if (can_finish && bfs_resolve_late(res) != 0) {
		goto fail;
	}

	return 0;

fail:
	bfs_resolve_free(res);
	return -1;
}

#if _POSIX_SPAWN > 0

/** bfs_spawn() implementation using posix_spawn(). */
static pid_t bfs_posix_spawn(struct bfs_resolver *res, const struct bfs_spawn *ctx, char **argv, char **envp) {
	pid_t ret;

	if (res->done) {
		errno = posix_spawn(&ret, res->exe, &ctx->actions, &ctx->attr, argv, envp);
	} else {
		errno = posix_spawnp(&ret, res->exe, &ctx->actions, &ctx->attr, argv, envp);
	}

	if (errno != 0) {
		return -1;
	}

	return ret;
}

/** Check if we can use posix_spawn(). */
static bool bfs_use_posix_spawn(const struct bfs_resolver *res, const struct bfs_spawn *ctx) {
	if (!(ctx->flags & BFS_SPAWN_USE_POSIX)) {
		return false;
	}

#if !BFS_POSIX_SPAWNP_AFTER_FCHDIR
	if (!res->done) {
		return false;
	}
#endif

	return true;
}

#endif // _POSIX_SPAWN > 0

/** Actually exec() the new process. */
static noreturn void bfs_spawn_exec(struct bfs_resolver *res, const struct bfs_spawn *ctx, char **argv, char **envp, int pipefd[2]) {
	xclose(pipefd[0]);

	for_slist (const struct bfs_spawn_action, action, ctx) {
		int fd;

		// Move the error-reporting pipe out of the way if necessary...
		if (action->out_fd == pipefd[1]) {
			fd = dup_cloexec(pipefd[1]);
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
		case BFS_SPAWN_OPEN:
			fd = open(action->path, action->flags, action->mode);
			if (fd < 0) {
				goto fail;
			}
			if (fd != action->out_fd) {
				if (dup2(fd, action->out_fd) < 0) {
					goto fail;
				}
			}
			break;
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

	if (bfs_resolve_late(res) != 0) {
		goto fail;
	}

	execve(res->exe, argv, envp);

fail:;
	int error = errno;

	// In case of a write error, the parent will still see that we exited
	// unsuccessfully, but won't know why
	(void)xwrite(pipefd[1], &error, sizeof(error));

	xclose(pipefd[1]);
	_Exit(127);
}

/** bfs_spawn() implementation using fork()/exec(). */
static pid_t bfs_fork_spawn(struct bfs_resolver *res, const struct bfs_spawn *ctx, char **argv, char **envp) {
	// Use a pipe to report errors from the child
	int pipefd[2];
	if (pipe_cloexec(pipefd) != 0) {
		return -1;
	}

	// Block signals before fork() so handlers don't run in the child
	sigset_t new_mask;
	if (sigfillset(&new_mask) != 0) {
		goto fail;
	}
	sigset_t old_mask;
	errno = pthread_sigmask(SIG_BLOCK, &new_mask, &old_mask);
	if (errno != 0) {
		goto fail;
	}

	pid_t pid = fork();
	if (pid == 0) {
		// Child
		bfs_spawn_exec(res, ctx, argv, envp, pipefd);
	}

	// Restore the original signal mask
	int ret = pthread_sigmask(SIG_SETMASK, &old_mask, NULL);
	bfs_verify(ret == 0, "pthread_sigmask(): %s", xstrerror(ret));

	if (pid < 0) {
		// fork() failed
		goto fail;
	}

	xclose(pipefd[1]);

	int error;
	ssize_t nbytes = xread(pipefd[0], &error, sizeof(error));
	xclose(pipefd[0]);
	if (nbytes == sizeof(error)) {
		xwaitpid(pid, NULL, 0);
		errno = error;
		return -1;
	}

	return pid;

fail:
	close_quietly(pipefd[1]);
	close_quietly(pipefd[0]);
	return -1;
}

/** Call the right bfs_spawn() implementation. */
static pid_t bfs_spawn_impl(struct bfs_resolver *res, const struct bfs_spawn *ctx, char **argv, char **envp) {
#if _POSIX_SPAWN > 0
	if (bfs_use_posix_spawn(res, ctx)) {
		return bfs_posix_spawn(res, ctx, argv, envp);
	}
#endif

	return bfs_fork_spawn(res, ctx, argv, envp);
}

pid_t bfs_spawn(const char *exe, const struct bfs_spawn *ctx, char **argv, char **envp) {
	// execvp()/posix_spawnp() are typically implemented with repeated
	// execv() calls for each $PATH component until one succeeds.  It's
	// faster to resolve the full path ahead of time.
	struct bfs_resolver res;
	if (bfs_resolve_early(&res, exe, ctx) != 0) {
		return -1;
	}

	extern char **environ;
	if (!envp) {
		envp = environ;
	}

	pid_t ret = bfs_spawn_impl(&res, ctx, argv, envp);
	bfs_resolve_free(&res);
	return ret;
}

char *bfs_spawn_resolve(const char *exe) {
	struct bfs_resolver res;
	if (bfs_resolve_early(&res, exe, NULL) != 0) {
		return NULL;
	}
	if (bfs_resolve_late(&res) != 0) {
		bfs_resolve_free(&res);
		return NULL;
	}

	char *ret;
	if (res.exe == res.buf) {
		ret = res.buf;
		res.buf = NULL;
	} else {
		ret = strdup(res.exe);
	}

	bfs_resolve_free(&res);
	return ret;
}
