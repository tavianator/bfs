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
#include "bfs.h"
#include "bftw.h"
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
	bfs_exec_debug(execbuf, "ARG_MAX: %ld remaining after environment variables\n", arg_max);

	// Account for the non-placeholder arguments
	for (size_t i = 0; i < execbuf->placeholder; ++i) {
		arg_max -= bfs_exec_arg_size(execbuf->tmpl_argv[i]);
	}
	for (size_t i = execbuf->placeholder + 1; i < execbuf->tmpl_argc; ++i) {
		arg_max -= bfs_exec_arg_size(execbuf->tmpl_argv[i]);
	}
	bfs_exec_debug(execbuf, "ARG_MAX: %ld remaining after fixed arguments\n", arg_max);

	// POSIX recommends subtracting 2048, for some wiggle room
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
	execbuf->placeholder = 0;
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
	const char *arg;
	for (i = 1, arg = argv[i]; arg; arg = argv[++i]) {
		if (strcmp(arg, ";") == 0) {
			break;
		} else if (strcmp(arg, "+") == 0) {
			execbuf->flags |= BFS_EXEC_MULTI;
			break;
		}
	}
	if (!arg) {
		cfprintf(cerr, "%{er}error: %s: Expected ';' or '+'.%{rs}\n", argv[0]);
		goto fail;
	}

	if ((execbuf->flags & BFS_EXEC_CONFIRM) && (execbuf->flags & BFS_EXEC_MULTI)) {
		cfprintf(cerr, "%{er}error: %s ... + is not supported.%{rs}\n", argv[0]);
		goto fail;
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
		for (i = 0; i < execbuf->tmpl_argc; ++i) {
			if (strstr(execbuf->tmpl_argv[i], "{}")) {
				execbuf->placeholder = i;
				break;
			}
		}
		if (i == execbuf->tmpl_argc) {
			cfprintf(cerr, "%{er}error: %s ... +: Expected '{}'.%{rs}\n", argv[0]);
			goto fail;
		}
		for (++i; i < execbuf->tmpl_argc; ++i) {
			if (strstr(execbuf->tmpl_argv[i], "{}")) {
				cfprintf(cerr, "%{er}error: %s ... +: Only one '{}' is supported.%{rs}\n", argv[0]);
				goto fail;
			}
		}

		execbuf->arg_max = bfs_exec_arg_max(execbuf);

		for (i = 0; i < execbuf->placeholder; ++i) {
			execbuf->argv[i] = execbuf->tmpl_argv[i];
		}
		execbuf->argc = i;
	}

	return execbuf;

fail:
	free_bfs_exec(execbuf);
	return NULL;
}

/** Format the current path for use as a command line argument. */
static const char *bfs_exec_format_path(const struct bfs_exec *execbuf, const struct BFTW *ftwbuf) {
	if (!(execbuf->flags & BFS_EXEC_CHDIR)) {
		return ftwbuf->path;
	}

	const char *name = ftwbuf->path + ftwbuf->nameoff;

	if (name[0] == '/') {
		// Must be a root path ("/", "//", etc.)
		return name;
	}

	// For compatibility with GNU find, use './name' instead of just 'name'
	char *path = dstralloc(2 + strlen(name));
	if (!path) {
		return NULL;
	}

	if (dstrcat(&path, "./") != 0) {
		goto err;
	}
	if (dstrcat(&path, name) != 0) {
		goto err;
	}

	return path;

err:
	dstrfree(path);
	return NULL;
}

/** Free a formatted path. */
static void bfs_exec_free_path(const char *path, const struct BFTW *ftwbuf) {
	if (path != ftwbuf->path && path != ftwbuf->path + ftwbuf->nameoff) {
		dstrfree((char *)path);
	}
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

	bfs_exec_debug(execbuf, "Executing '%s' ... [%zu arguments]\n", execbuf->argv[0], execbuf->argc - 1);

	pid_t pid = fork();

	if (pid < 0) {
		return -1;
	} else if (pid > 0) {
		int status;
		if (waitpid(pid, &status, 0) < 0) {
			return -1;
		}

		errno = 0;
		if (WIFEXITED(status) && WEXITSTATUS(status) == EXIT_SUCCESS) {
			return 0;
		} else {
			return -1;
		}
	} else {
		if (execbuf->wd_fd >= 0) {
			if (fchdir(execbuf->wd_fd) != 0) {
				perror("fchdir()");
				goto fail;
			}
		}

		execvp(execbuf->argv[0], execbuf->argv);
		perror("execvp()");
	}

fail:
	_Exit(EXIT_FAILURE);
}

/** exec() a command for a single file. */
static int bfs_exec_single(struct bfs_exec *execbuf, const struct BFTW *ftwbuf) {
	int ret = -1, error = 0;

	const char *path = bfs_exec_format_path(execbuf, ftwbuf);
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

	bfs_exec_free_path(path, ftwbuf);

	errno = error;

out:
	return ret;
}

/** Execute the pending command from a BFS_EXEC_MULTI execbuf. */
static int bfs_exec_flush(struct bfs_exec *execbuf) {
	int ret = 0;

	size_t last_path = execbuf->argc;
	if (last_path > execbuf->placeholder) {
		for (size_t i = execbuf->placeholder + 1; i < execbuf->tmpl_argc; ++i, ++execbuf->argc) {
			execbuf->argv[execbuf->argc] = execbuf->tmpl_argv[i];
		}
		execbuf->argv[execbuf->argc] = NULL;

		if (bfs_exec_spawn(execbuf) != 0) {
			ret = -1;
		}
	}

	int error = errno;

	bfs_exec_closewd(execbuf, NULL);

	for (size_t i = execbuf->placeholder; i < last_path; ++i) {
		bfs_exec_free_arg(execbuf->argv[i], execbuf->tmpl_argv[execbuf->placeholder]);
	}
	execbuf->argc = execbuf->placeholder;
	execbuf->arg_size = 0;

	errno = error;
	return ret;
}

/** Check if a flush is needed before a new file is processed. */
static bool bfs_exec_needs_flush(struct bfs_exec *execbuf, const struct BFTW *ftwbuf, const char *arg) {
	if (execbuf->flags & BFS_EXEC_CHDIR) {
		if (ftwbuf->nameoff > execbuf->wd_len
		    || (execbuf->wd_path && strncmp(ftwbuf->path, execbuf->wd_path, execbuf->wd_len) != 0)) {
			bfs_exec_debug(execbuf, "Changed directories, executing buffered command\n");
			return true;
		}
	}

	if (execbuf->arg_size + bfs_exec_arg_size(arg) > execbuf->arg_max) {
		bfs_exec_debug(execbuf, "Reached max command size, executing buffered command\n");
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

	const char *path = bfs_exec_format_path(execbuf, ftwbuf);
	if (!path) {
		ret = -1;
		goto out;
	}

	char *arg = bfs_exec_format_arg(execbuf->tmpl_argv[execbuf->placeholder], path);
	if (!arg) {
		ret = -1;
		goto out_path;
	}

	if (bfs_exec_needs_flush(execbuf, ftwbuf, arg)) {
		if (bfs_exec_flush(execbuf) != 0) {
			ret = -1;
		}
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
	goto out_path;

out_arg:
	bfs_exec_free_arg(arg, execbuf->tmpl_argv[execbuf->placeholder]);
out_path:
	bfs_exec_free_path(path, ftwbuf);
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
		if (bfs_exec_flush(execbuf) != 0) {
			execbuf->ret = -1;
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
