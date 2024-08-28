// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include "tests.h"

#include "alloc.h"
#include "bfstd.h"
#include "dstring.h"
#include "xspawn.h"

#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

/** Duplicate the current environment. */
static char **envdup(void) {
	extern char **environ;

	char **envp = NULL;
	size_t envc = 0;

	for (char **var = environ; ; ++var) {
		char *copy = NULL;
		if (*var) {
			copy = strdup(*var);
			if (!copy) {
				goto fail;
			}
		}

		char **dest = RESERVE(char *, &envp, &envc);
		if (!dest) {
			free(copy);
			goto fail;
		}
		*dest = copy;

		if (!*var) {
			break;
		}
	}

	return envp;

fail:
	for (size_t i = 0; i < envc; ++i) {
		free(envp[i]);
	}
	free(envp);
	return NULL;
}

/** Check that we resolve executables in $PATH correctly. */
static void check_use_path(bool use_posix) {
	struct bfs_spawn spawn;
	if (!bfs_echeck(bfs_spawn_init(&spawn) == 0)) {
		return;
	}

	spawn.flags |= BFS_SPAWN_USE_PATH;
	if (!use_posix) {
		spawn.flags &= ~BFS_SPAWN_USE_POSIX;
	}

	bool init = bfs_echeck(bfs_spawn_addopen(&spawn, 10, "bin", O_RDONLY | O_DIRECTORY, 0) == 0)
		&& bfs_echeck(bfs_spawn_adddup2(&spawn, 10, 11) == 0)
		&& bfs_echeck(bfs_spawn_addclose(&spawn, 10) == 0)
		&& bfs_echeck(bfs_spawn_addfchdir(&spawn, 11) == 0)
		&& bfs_echeck(bfs_spawn_addclose(&spawn, 11) == 0);
	if (!init) {
		goto destroy;
	}

	// Check that $PATH is resolved in the parent's environment
	char **envp = envdup();
	if (!bfs_echeck(envp, "envdup()")) {
		goto destroy;
	}

	// Check that $PATH is resolved after the file actions
	char *old_path = getenv("PATH");
	dchar *new_path = NULL;
	if (old_path) {
		old_path = strdup(old_path);
		if (!bfs_echeck(old_path, "strdup()")) {
			goto env;
		}
		new_path = dstrprintf("tests:%s", old_path);
	} else {
		new_path = dstrdup("tests");
	}
	if (!bfs_check(new_path)) {
		goto path;
	}

	if (!bfs_echeck(setenv("PATH", new_path, true) == 0)) {
		goto path;
	}

	char *argv[] = {"xspawnee", old_path, NULL};
	pid_t pid = bfs_spawn("xspawnee", &spawn, argv, envp);
	if (!bfs_echeck(pid >= 0, "bfs_spawn()")) {
		goto unset;
	}

	int wstatus;
	bool exited = bfs_echeck(xwaitpid(pid, &wstatus, 0) == pid)
		&& bfs_check(WIFEXITED(wstatus));
	if (exited) {
		int wexit = WEXITSTATUS(wstatus);
		bfs_check(wexit == EXIT_SUCCESS, "xspawnee: exit(%d)", wexit);
	}

unset:
	if (old_path) {
		bfs_echeck(setenv("PATH", old_path, true) == 0);
	} else {
		bfs_echeck(unsetenv("PATH") == 0);
	}
path:
	dstrfree(new_path);
	free(old_path);
env:
	for (char **var = envp; *var; ++var) {
		free(*var);
	}
	free(envp);
destroy:
	bfs_echeck(bfs_spawn_destroy(&spawn) == 0);
}

/** Check path resolution of non-existent executables. */
static void check_enoent(bool use_posix) {
	struct bfs_spawn spawn;
	if (!bfs_echeck(bfs_spawn_init(&spawn) == 0)) {
		return;
	}

	spawn.flags |= BFS_SPAWN_USE_PATH;
	if (!use_posix) {
		spawn.flags &= ~BFS_SPAWN_USE_POSIX;
	}

	char *argv[] = {"eW6f5RM9Qi", NULL};
	pid_t pid = bfs_spawn("eW6f5RM9Qi", &spawn, argv, NULL);
	bfs_echeck(pid < 0 && errno == ENOENT, "bfs_spawn()");

	bfs_echeck(bfs_spawn_destroy(&spawn) == 0);
}

static void check_resolve(void) {
	char *exe;

	exe = bfs_spawn_resolve("sh");
	bfs_echeck(exe, "bfs_spawn_resolve('sh')");
	free(exe);

	exe = bfs_spawn_resolve("/bin/sh");
	bfs_echeck(exe && strcmp(exe, "/bin/sh") == 0);
	free(exe);

	exe = bfs_spawn_resolve("bin/tests/xspawnee");
	bfs_echeck(exe && strcmp(exe, "bin/tests/xspawnee") == 0);
	free(exe);

	bfs_echeck(!bfs_spawn_resolve("eW6f5RM9Qi") && errno == ENOENT);

	bfs_echeck(!bfs_spawn_resolve("bin/eW6f5RM9Qi") && errno == ENOENT);
}

void check_xspawn(void) {
	check_use_path(true);
	check_use_path(false);

	check_enoent(true);
	check_enoent(false);

	check_resolve();
}
