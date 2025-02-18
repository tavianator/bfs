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

/** Add an entry to $PATH. */
static int add_path(const char *entry, char **old_path) {
	int ret = -1;
	const char *new_path = NULL;

	*old_path = getenv("PATH");
	if (*old_path) {
		*old_path = strdup(*old_path);
		if (!*old_path) {
			goto done;
		}

		new_path = dstrprintf("%s:%s", entry, *old_path);
		if (!new_path) {
			goto done;
		}
	} else {
		new_path = entry;
	}

	ret = setenv("PATH", new_path, true);

done:
	if (new_path && new_path != entry) {
		dstrfree((dchar *)new_path);
	}

	if (ret != 0) {
		free(*old_path);
		*old_path = NULL;
	}

	return ret;
}

/** Undo add_path(). */
static int reset_path(char *old_path) {
	int ret;

	if (old_path) {
		ret = setenv("PATH", old_path, true);
		free(old_path);
	} else {
		ret = unsetenv("PATH");
	}

	return ret;
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
	char *old_path;
	if (!bfs_echeck(add_path("tests", &old_path) == 0)) {
		goto env;
	}

	char *argv[] = {"xspawnee", old_path, NULL};
	pid_t pid = bfs_spawn("xspawnee", &spawn, argv, envp);
	if (!bfs_echeck(pid >= 0, "bfs_spawn()")) {
		goto path;
	}

	int wstatus;
	bool exited = bfs_echeck(xwaitpid(pid, &wstatus, 0) == pid)
		&& bfs_check(WIFEXITED(wstatus));
	if (exited) {
		int wexit = WEXITSTATUS(wstatus);
		bfs_check(wexit == EXIT_SUCCESS, "xspawnee: exit(%d)", wexit);
	}

path:
	bfs_echeck(reset_path(old_path) == 0);
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

	char *old_path;
	if (bfs_echeck(add_path("bin/tests", &old_path) == 0)) {
		exe = bfs_spawn_resolve("xspawnee");
		bfs_echeck(exe && strcmp(exe, "bin/tests/xspawnee") == 0);
		free(exe);
		bfs_echeck(reset_path(old_path) == 0);
	}
}

void check_xspawn(void) {
	check_use_path(true);
	check_use_path(false);

	check_enoent(true);
	check_enoent(false);

	check_resolve();
}
