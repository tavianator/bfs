/****************************************************************************
 * bfs                                                                      *
 * Copyright (C) 2017 Tavian Barnes <tavianator@tavianator.com>             *
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

#include "exec.h"
#include "bftw.h"
#include "cmdline.h"
#include "color.h"
#include "dstring.h"
#include "util.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/** Print some debugging info. */
static void bfs_exec_debug(const struct bfs_exec *execbuf, const char *format, ...) {
	if (!(execbuf->flags & BFS_EXEC_DEBUG)) {
		return;
	}

	if (execbuf->flags & BFS_EXEC_CONFIRM) {
		fputs("-ok", stderr);
	} else {
		fputs("-exec", stderr);
	}
	if (execbuf->flags & BFS_EXEC_CHDIR) {
		fputs("dir", stderr);
	}
	fputs(": ", stderr);

	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
}

extern char **environ;

/** Determine the size of a single argument, for comparison to arg_max. */
static size_t bfs_exec_arg_size(const char *arg) {
	return sizeof(arg) + strlen(arg) + 1;
}

/** Even if we can pass a bigger argument list, cap it here. */
#define BFS_EXEC_ARG_MAX (16*1024*1024)

/** Determine the maximum argv size. */
static size_t bfs_exec_arg_max(const struct bfs_exec *execbuf) {
	long arg_max = sysconf(_SC_ARG_MAX);
	bfs_exec_debug(execbuf, "ARG_MAX: %ld according to sysconf()\n", arg_max);
	if (arg_max < 0) {
		arg_max = BFS_EXEC_ARG_MAX;
		bfs_exec_debug(execbuf, "ARG_MAX: %ld assumed\n", arg_max);
	}

	// We have to share space with the environment variables
	for (char **envp = environ; *envp; ++envp) {
		arg_max -= bfs_exec_arg_size(*envp);
	}
	// Account for the terminating NULL entry
	arg_max -= sizeof(char *);
	bfs_exec_debug(execbuf, "ARG_MAX: %ld remaining after environment variables\n", arg_max);

	// Account for the fixed arguments
	for (size_t i = 0; i < execbuf->tmpl_argc - 1; ++i) {
		arg_max -= bfs_exec_arg_size(execbuf->tmpl_argv[i]);
	}
	// Account for the terminating NULL entry
	arg_max -= sizeof(char *);
	bfs_exec_debug(execbuf, "ARG_MAX: %ld remaining after fixed arguments\n", arg_max);

	// Assume arguments are counted with the granularity of a single page,
	// so allow a one page cushion to account for rounding up
	long page_size = sysconf(_SC_PAGESIZE);
	if (page_size < 4096) {
		page_size = 4096;
	}
	arg_max -= page_size;
	bfs_exec_debug(execbuf, "ARG_MAX: %ld remaining after page cushion\n", arg_max);

	// POSIX recommends an additional 2048 bytes of headroom
	arg_max -= 2048;
	bfs_exec_debug(execbuf, "ARG_MAX: %ld remaining after headroom\n", arg_max);

	if (arg_max < 0) {
		arg_max = 0;
	} else if (arg_max > BFS_EXEC_ARG_MAX) {
		arg_max = BFS_EXEC_ARG_MAX;
	}

	bfs_exec_debug(execbuf, "ARG_MAX: %ld final value\n", arg_max);
	return arg_max;
}

struct bfs_exec *parse_bfs_exec(char **argv, enum bfs_exec_flags flags, const struct cmdline *cmdline) {
	CFILE *cerr = cmdline->cerr;

	struct bfs_exec *execbuf = malloc(sizeof(*execbuf));
	if (!execbuf) {
		perror("malloc()");
		goto fail;
	}

	execbuf->flags = flags;
	execbuf->argv = NULL;
	execbuf->argc = 0;
	execbuf->argv_cap = 0;
	execbuf->arg_size = 0;
	execbuf->arg_max = 0;
	execbuf->wd_fd = -1;
	execbuf->wd_path = NULL;
	execbuf->wd_len = 0;
	execbuf->ret = 0;

	if (cmdline->debug & DEBUG_EXEC) {
		execbuf->flags |= BFS_EXEC_DEBUG;
	}

	size_t i;
	for (i = 1; ; ++i) {
		const char *arg = argv[i];
		if (!arg) {
			if (execbuf->flags & BFS_EXEC_CONFIRM) {
				cfprintf(cerr, "%{er}error: %s: Expected '... ;'.%{rs}\n", argv[0]);
			} else {
				cfprintf(cerr, "%{er}error: %s: Expected '... ;' or '... {} +'.%{rs}\n", argv[0]);
			}
			goto fail;
		} else if (strcmp(arg, ";") == 0) {
			break;
		} else if (strcmp(arg, "+") == 0) {
			if (!(execbuf->flags & BFS_EXEC_CONFIRM) && strcmp(argv[i - 1], "{}") == 0) {
				execbuf->flags |= BFS_EXEC_MULTI;
				break;
			}
		}
	}

	execbuf->tmpl_argv = argv + 1;
	execbuf->tmpl_argc = i - 1;

	execbuf->argv_cap = execbuf->tmpl_argc + 1;
	execbuf->argv = malloc(execbuf->argv_cap*sizeof(*execbuf->argv));
	if (!execbuf->argv) {
		perror("malloc()");
		goto fail;
	}

	if (execbuf->flags & BFS_EXEC_MULTI) {
		for (i = 0; i < execbuf->tmpl_argc - 1; ++i) {
			char *arg = execbuf->tmpl_argv[i];
			if (strstr(arg, "{}")) {
				cfprintf(cerr, "%{er}error: %s ... +: Only one '{}' is supported.%{rs}\n", argv[0]);
				goto fail;
			}
			execbuf->argv[i] = arg;
		}
		execbuf->argc = execbuf->tmpl_argc - 1;

		execbuf->arg_max = bfs_exec_arg_max(execbuf);
	}

	return execbuf;

fail:
	free_bfs_exec(execbuf);
	return NULL;
}

/** Format the current path for use as a command line argument. */
static char *bfs_exec_format_path(const struct bfs_exec *execbuf, const struct BFTW *ftwbuf) {
	if (!(execbuf->flags & BFS_EXEC_CHDIR)) {
		return strdup(ftwbuf->path);
	}

	const char *name = ftwbuf->path + ftwbuf->nameoff;

	if (name[0] == '/') {
		// Must be a root path ("/", "//", etc.)
		return strdup(name);
	}

	// For compatibility with GNU find, use './name' instead of just 'name'
	char *path = malloc(2 + strlen(name) + 1);
	if (!path) {
		return NULL;
	}

	strcpy(path, "./");
	strcpy(path + 2, name);

	return path;
}

/** Format an argument, expanding "{}" to the current path. */
static char *bfs_exec_format_arg(char *arg, const char *path) {
	char *match = strstr(arg, "{}");
	if (!match) {
		return arg;
	}

	char *ret = dstralloc(0);
	if (!ret) {
		return NULL;
	}

	char *last = arg;
	do {
		if (dstrncat(&ret, last, match - last) != 0) {
			goto err;
		}
		if (dstrcat(&ret, path) != 0) {
			goto err;
		}

		last = match + 2;
		match = strstr(last, "{}");
	} while (match);

	if (dstrcat(&ret, last) != 0) {
		goto err;
	}

	return ret;

err:
	dstrfree(ret);
	return NULL;
}

/** Free a formatted argument. */
static void bfs_exec_free_arg(char *arg, const char *tmpl) {
	if (arg != tmpl) {
		dstrfree(arg);
	}
}

/** Open a file to use as the working directory. */
static int bfs_exec_openwd(struct bfs_exec *execbuf, const struct BFTW *ftwbuf) {
	assert(execbuf->wd_fd < 0);
	assert(!execbuf->wd_path);

	if (ftwbuf->at_fd != AT_FDCWD) {
		execbuf->wd_fd = ftwbuf->at_fd;
		if (!(execbuf->flags & BFS_EXEC_MULTI)) {
			return 0;
		}

		execbuf->wd_fd = dup_cloexec(execbuf->wd_fd);
		if (execbuf->wd_fd < 0) {
			return -1;
		}
	}

	execbuf->wd_len = ftwbuf->nameoff;
	if (execbuf->wd_len == 0) {
		if (ftwbuf->path[0] == '/') {
			++execbuf->wd_len;
		} else {
			// The path is something like "foo", so we're already in the right directory
			return 0;
		}
	}

	execbuf->wd_path = strndup(ftwbuf->path, execbuf->wd_len);
	if (!execbuf->wd_path) {
		return -1;
	}

	if (execbuf->wd_fd < 0) {
		execbuf->wd_fd = open(execbuf->wd_path, O_RDONLY | O_CLOEXEC | O_DIRECTORY);
	}

	if (execbuf->wd_fd < 0) {
		return -1;
	}

	return 0;
}

/** Close the working directory. */
static int bfs_exec_closewd(struct bfs_exec *execbuf, const struct BFTW *ftwbuf) {
	int ret = 0;

	if (execbuf->wd_fd >= 0) {
		if (!ftwbuf || execbuf->wd_fd != ftwbuf->at_fd) {
			ret = close(execbuf->wd_fd);
		}
		execbuf->wd_fd = -1;
	}

	if (execbuf->wd_path) {
		free(execbuf->wd_path);
		execbuf->wd_path = NULL;
		execbuf->wd_len = 0;
	}

	return ret;
}

/** Actually spawn the process. */
static int bfs_exec_spawn(const struct bfs_exec *execbuf) {
	if (execbuf->flags & BFS_EXEC_CONFIRM) {
		for (size_t i = 0; i < execbuf->argc; ++i) {
			fprintf(stderr, "%s ", execbuf->argv[i]);
		}
		fprintf(stderr, "? ");

		if (ynprompt() <= 0) {
			errno = 0;
			return -1;
		}
	}

	if (execbuf->flags & BFS_EXEC_MULTI) {
		bfs_exec_debug(execbuf, "Executing '%s' ... [%zu arguments] (size %zu)\n",
		               execbuf->argv[0], execbuf->argc - 1, execbuf->arg_size);
	} else {
		bfs_exec_debug(execbuf, "Executing '%s' ... [%zu arguments]\n", execbuf->argv[0], execbuf->argc - 1);
	}

	// Use a pipe to report errors from the child
	int pipefd[2] = {-1, -1};
	if (pipe_cloexec(pipefd) != 0) {
		bfs_exec_debug(execbuf, "pipe() failed: %s\n", strerror(errno));
	}

	pid_t pid = fork();

	if (pid < 0) {
		close(pipefd[1]);
		close(pipefd[0]);
		return -1;
	} else if (pid > 0) {
		// Parent
		close(pipefd[1]);

		int error;
		ssize_t nbytes = read(pipefd[0], &error, sizeof(error));
		close(pipefd[0]);
		if (nbytes == sizeof(error)) {
			errno = error;
			return -1;
		}

		int wstatus;
		if (waitpid(pid, &wstatus, 0) < 0) {
			return -1;
		}

		errno = 0;

		if (WIFEXITED(wstatus)) {
			int status = WEXITSTATUS(wstatus);
			if (status == EXIT_SUCCESS) {
				return 0;
			} else {
				bfs_exec_debug(execbuf, "Command '%s' failed with status %d\n", execbuf->argv[0], status);
			}
		} else if (WIFSIGNALED(wstatus)) {
			int sig = WTERMSIG(wstatus);
			bfs_exec_debug(execbuf, "Command '%s' terminated by signal %d\n", execbuf->argv[0], sig);
		} else {
			bfs_exec_debug(execbuf, "Command '%s' terminated abnormally\n", execbuf->argv[0]);
		}

		return -1;
	} else {
		// Child
		close(pipefd[0]);

		if (execbuf->wd_fd >= 0) {
			if (fchdir(execbuf->wd_fd) != 0) {
				goto child_err;
			}
		}

		execvp(execbuf->argv[0], execbuf->argv);

		int error;
	child_err:
		error = errno;
		if (write(pipefd[1], &error, sizeof(error)) != sizeof(error)) {
			// Parent will still see that we exited unsuccessfully, but won't know why
		}
		close(pipefd[1]);
		_Exit(EXIT_FAILURE);
	}

}

/** exec() a command for a single file. */
static int bfs_exec_single(struct bfs_exec *execbuf, const struct BFTW *ftwbuf) {
	int ret = -1, error = 0;

	char *path = bfs_exec_format_path(execbuf, ftwbuf);
	if (!path) {
		goto out;
	}

	size_t i;
	for (i = 0; i < execbuf->tmpl_argc; ++i) {
		execbuf->argv[i] = bfs_exec_format_arg(execbuf->tmpl_argv[i], path);
		if (!execbuf->argv[i]) {
			goto out_free;
		}
	}
	execbuf->argv[i] = NULL;
	execbuf->argc = i;

	if (execbuf->flags & BFS_EXEC_CHDIR) {
		if (bfs_exec_openwd(execbuf, ftwbuf) != 0) {
			goto out_free;
		}
	}

	ret = bfs_exec_spawn(execbuf);

out_free:
	error = errno;

	bfs_exec_closewd(execbuf, ftwbuf);

	for (size_t j = 0; j < i; ++j) {
		bfs_exec_free_arg(execbuf->argv[j], execbuf->tmpl_argv[j]);
	}

	free(path);

	errno = error;

out:
	return ret;
}

/** Check if any arguments remain in the buffer. */
static bool bfs_exec_args_remain(const struct bfs_exec *execbuf) {
	return execbuf->argc >= execbuf->tmpl_argc;
}

/** Execute the pending command from a BFS_EXEC_MULTI execbuf. */
static int bfs_exec_flush(struct bfs_exec *execbuf) {
	int ret = 0, error = 0;

	size_t orig_argc = execbuf->argc;
	while (bfs_exec_args_remain(execbuf)) {
		execbuf->argv[execbuf->argc] = NULL;
		ret = bfs_exec_spawn(execbuf);
		error = errno;
		if (ret == 0 || error != E2BIG) {
			break;
		}

		// Try to recover from E2BIG by trying fewer and fewer arguments
		// until they fit
		bfs_exec_debug(execbuf, "Got E2BIG, shrinking argument list...\n");
		execbuf->argv[execbuf->argc] = execbuf->argv[execbuf->argc - 1];
		execbuf->arg_size -= bfs_exec_arg_size(execbuf->argv[execbuf->argc]);
		--execbuf->argc;
	}
	size_t new_argc = execbuf->argc;
	size_t new_size = execbuf->arg_size;

	for (size_t i = execbuf->tmpl_argc - 1; i < new_argc; ++i) {
		free(execbuf->argv[i]);
	}
	execbuf->argc = execbuf->tmpl_argc - 1;
	execbuf->arg_size = 0;

	if (new_argc < orig_argc) {
		execbuf->arg_max = new_size;
		bfs_exec_debug(execbuf, "ARG_MAX: %zu\n", execbuf->arg_max);

		// If we recovered from E2BIG, there are unused arguments at the
		// end of the list
		for (size_t i = new_argc + 1; i <= orig_argc; ++i) {
			if (error == 0) {
				execbuf->argv[execbuf->argc] = execbuf->argv[i];
				execbuf->arg_size += bfs_exec_arg_size(execbuf->argv[execbuf->argc]);
				++execbuf->argc;
			} else {
				free(execbuf->argv[i]);
			}
		}
	}

	errno = error;
	return ret;
}

/** Check if we need to flush the execbuf because we're changing directories. */
static bool bfs_exec_changed_dirs(const struct bfs_exec *execbuf, const struct BFTW *ftwbuf) {
	if (execbuf->flags & BFS_EXEC_CHDIR) {
		if (ftwbuf->nameoff > execbuf->wd_len
		    || (execbuf->wd_path && strncmp(ftwbuf->path, execbuf->wd_path, execbuf->wd_len) != 0)) {
			bfs_exec_debug(execbuf, "Changed directories, executing buffered command\n");
			return true;
		}
	}

	return false;
}

/** Check if we need to flush the execbuf because we're too big. */
static bool bfs_exec_would_overflow(const struct bfs_exec *execbuf, const char *arg) {
	size_t next_size = execbuf->arg_size + bfs_exec_arg_size(arg);
	if (next_size > execbuf->arg_max) {
		bfs_exec_debug(execbuf, "Command size (%zu) would exceed maximum (%zu), executing buffered command\n",
		               next_size, execbuf->arg_max);
		return true;
	}

	return false;
}

/** Push a new argument to a BFS_EXEC_MULTI execbuf. */
static int bfs_exec_push(struct bfs_exec *execbuf, char *arg) {
	execbuf->argv[execbuf->argc] = arg;

	if (execbuf->argc + 1 >= execbuf->argv_cap) {
		size_t cap = 2*execbuf->argv_cap;
		char **argv = realloc(execbuf->argv, cap*sizeof(*argv));
		if (!argv) {
			return -1;
		}
		execbuf->argv = argv;
		execbuf->argv_cap = cap;
	}

	++execbuf->argc;
	execbuf->arg_size += bfs_exec_arg_size(arg);
	return 0;
}

/** Handle a new path for a BFS_EXEC_MULTI execbuf. */
static int bfs_exec_multi(struct bfs_exec *execbuf, const struct BFTW *ftwbuf) {
	int ret = 0;

	char *arg = bfs_exec_format_path(execbuf, ftwbuf);
	if (!arg) {
		ret = -1;
		goto out;
	}

	if (bfs_exec_changed_dirs(execbuf, ftwbuf)) {
		while (bfs_exec_args_remain(execbuf)) {
			ret |= bfs_exec_flush(execbuf);
		}
		bfs_exec_closewd(execbuf, ftwbuf);
	} else if (bfs_exec_would_overflow(execbuf, arg)) {
		ret |= bfs_exec_flush(execbuf);
	}

	if ((execbuf->flags & BFS_EXEC_CHDIR) && execbuf->wd_fd < 0) {
		if (bfs_exec_openwd(execbuf, ftwbuf) != 0) {
			ret = -1;
			goto out_arg;
		}
	}

	if (bfs_exec_push(execbuf, arg) != 0) {
		ret = -1;
		goto out_arg;
	}

	// arg will get cleaned up later by bfs_exec_flush()
	goto out;

out_arg:
	free(arg);
out:
	return ret;
}

int bfs_exec(struct bfs_exec *execbuf, const struct BFTW *ftwbuf) {
	if (execbuf->flags & BFS_EXEC_MULTI) {
		if (bfs_exec_multi(execbuf, ftwbuf) == 0) {
			errno = 0;
		} else {
			execbuf->ret = -1;
		}
		// -exec ... + never returns false
		return 0;
	} else {
		return bfs_exec_single(execbuf, ftwbuf);
	}
}

int bfs_exec_finish(struct bfs_exec *execbuf) {
	if (execbuf->flags & BFS_EXEC_MULTI) {
		bfs_exec_debug(execbuf, "Finishing execution, executing buffered command\n");
		while (bfs_exec_args_remain(execbuf)) {
			execbuf->ret |= bfs_exec_flush(execbuf);
		}
		if (execbuf->ret != 0) {
			bfs_exec_debug(execbuf, "One or more executions of '%s' failed\n", execbuf->argv[0]);
		}
	}
	return execbuf->ret;
}

void free_bfs_exec(struct bfs_exec *execbuf) {
	if (execbuf) {
		bfs_exec_closewd(execbuf, NULL);
		free(execbuf->argv);
		free(execbuf);
	}
}
