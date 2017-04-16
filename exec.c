/*********************************************************************
 * bfs                                                               *
 * Copyright (C) 2017 Tavian Barnes <tavianator@tavianator.com>      *
 *                                                                   *
 * This program is free software. It comes without any warranty, to  *
 * the extent permitted by applicable law. You can redistribute it   *
 * and/or modify it under the terms of the Do What The Fuck You Want *
 * To Public License, Version 2, as published by Sam Hocevar. See    *
 * the COPYING file or http://www.wtfpl.net/ for more details.       *
 *********************************************************************/

#include "exec.h"
#include "bftw.h"
#include "color.h"
#include "dstring.h"
#include "util.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

struct bfs_exec *parse_bfs_exec(char **argv, enum bfs_exec_flags flags, CFILE *cerr) {
	struct bfs_exec *execbuf = malloc(sizeof(*execbuf));
	if (!execbuf) {
		perror("malloc()");
		goto fail;
	}

	execbuf->flags = flags;
	execbuf->placeholder = 0;
	execbuf->argv = NULL;
	execbuf->argc = 0;
	execbuf->wd_fd = -1;
	execbuf->wd_path = NULL;
	execbuf->wd_len = 0;
	execbuf->ret = 0;

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

	if (execbuf->flags & BFS_EXEC_MULTI) {
		long arg_max = sysconf(_SC_ARG_MAX);
		if (arg_max < 0) {
			execbuf->arg_max = _POSIX_ARG_MAX;
		} else {
			execbuf->arg_max = arg_max;
		}
	} else {
		execbuf->arg_max = execbuf->tmpl_argc;
	}

	execbuf->argv = malloc((execbuf->arg_max + 1)*sizeof(*execbuf->argv));
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
		int flags = O_RDONLY | O_CLOEXEC;
#ifdef O_DIRECTORY
		flags |= O_DIRECTORY;
#endif
		execbuf->wd_fd = open(execbuf->wd_path, flags);
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
		fflush(stderr);

		int c = getchar();
		bool exec = c == 'y' || c == 'Y';
		while (c != '\n' && c != EOF) {
			c = getchar();
		}
		if (!exec) {
			errno = 0;
			return -1;
		}
	}

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

/** Execute the pending command from a multi-execbuf. */
static void bfs_exec_flush(struct bfs_exec *execbuf) {
	size_t last_path = execbuf->argc;
	if (last_path > execbuf->placeholder) {
		for (size_t i = execbuf->placeholder + 1; i < execbuf->tmpl_argc; ++i, ++execbuf->argc) {
			execbuf->argv[execbuf->argc] = execbuf->tmpl_argv[i];
		}
		execbuf->argv[execbuf->argc] = NULL;

		if (bfs_exec_spawn(execbuf) != 0) {
			execbuf->ret = -1;
		}
	}

	bfs_exec_closewd(execbuf, NULL);

	for (size_t i = execbuf->placeholder; i < last_path; ++i) {
		bfs_exec_free_arg(execbuf->argv[i], execbuf->tmpl_argv[execbuf->placeholder]);
	}
	execbuf->argc = execbuf->placeholder;
}

/** Check if a flush is needed before a new file is processed. */
static bool bfs_exec_needs_flush(struct bfs_exec *execbuf, const struct BFTW *ftwbuf) {
	if (execbuf->flags & BFS_EXEC_CHDIR) {
		if (ftwbuf->nameoff > execbuf->wd_len) {
			return true;
		}
		if (execbuf->wd_path && strncmp(ftwbuf->path, execbuf->wd_path, execbuf->wd_len) != 0) {
			return true;
		}
	}

	size_t tail = execbuf->tmpl_argc - execbuf->placeholder - 1;
	if (execbuf->argc + tail >= execbuf->arg_max) {
		return true;
	}

	return false;
}

/** Push a new file to a multi-execbuf. */
static void bfs_exec_multi(struct bfs_exec *execbuf, const struct BFTW *ftwbuf) {
	if (bfs_exec_needs_flush(execbuf, ftwbuf)) {
		bfs_exec_flush(execbuf);
	}

	if ((execbuf->flags & BFS_EXEC_CHDIR) && execbuf->wd_fd < 0) {
		if (bfs_exec_openwd(execbuf, ftwbuf) != 0) {
			execbuf->ret = -1;
			return;
		}
	}

	const char *path = bfs_exec_format_path(execbuf, ftwbuf);
	if (!path) {
		execbuf->ret = -1;
		return;
	}

	char *arg = bfs_exec_format_arg(execbuf->tmpl_argv[execbuf->placeholder], path);
	if (!arg) {
		execbuf->ret = -1;
		goto out_path;
	}

	execbuf->argv[execbuf->argc++] = arg;

out_path:
	bfs_exec_free_path(path, ftwbuf);
}

int bfs_exec(struct bfs_exec *execbuf, const struct BFTW *ftwbuf) {
	if (execbuf->flags & BFS_EXEC_MULTI) {
		bfs_exec_multi(execbuf, ftwbuf);
		// -exec ... + never returns false
		return 0;
	} else {
		return bfs_exec_single(execbuf, ftwbuf);
	}
}

int bfs_exec_finish(struct bfs_exec *execbuf) {
	if (execbuf->flags & BFS_EXEC_MULTI) {
		bfs_exec_flush(execbuf);
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
