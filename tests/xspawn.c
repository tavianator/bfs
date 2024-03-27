// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include "tests.h"
#include "../src/alloc.h"
#include "../src/bfstd.h"
#include "../src/config.h"
#include "../src/dstring.h"
#include "../src/xspawn.h"
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
static bool check_use_path(bool use_posix) {
	bool ret = true;

	struct bfs_spawn spawn;
	ret &= bfs_pcheck(bfs_spawn_init(&spawn) == 0);
	if (!ret) {
		goto out;
	}

	spawn.flags |= BFS_SPAWN_USE_PATH;
	if (!use_posix) {
		spawn.flags &= ~BFS_SPAWN_USE_POSIX;
	}

	const char *builddir = getenv("BUILDDIR");
	dchar *bin = dstrprintf("%s/bin", builddir ? builddir : ".");
	ret &= bfs_pcheck(bin, "dstrprintf()");
	if (!ret) {
		goto destroy;
	}

	ret &= bfs_pcheck(bfs_spawn_addopen(&spawn, 10, bin, O_RDONLY | O_DIRECTORY, 0) == 0);
	ret &= bfs_pcheck(bfs_spawn_adddup2(&spawn, 10, 11) == 0);
	ret &= bfs_pcheck(bfs_spawn_addclose(&spawn, 10) == 0);
	ret &= bfs_pcheck(bfs_spawn_addfchdir(&spawn, 11) == 0);
	ret &= bfs_pcheck(bfs_spawn_addclose(&spawn, 11) == 0);
	if (!ret) {
		goto bin;
	}

	// Check that $PATH is resolved in the parent's environment
	char **envp;
	ret &= bfs_pcheck(envp = envdup());
	if (!ret) {
		goto bin;
	}

	// Check that $PATH is resolved after the file actions
	char *old_path = getenv("PATH");
	dchar *new_path = NULL;
	if (old_path) {
		ret &= bfs_pcheck(old_path = strdup(old_path));
		if (!ret) {
			goto env;
		}
		new_path = dstrprintf("tests:%s", old_path);
	} else {
		new_path = dstrdup("tests");
	}
	ret &= bfs_check(new_path);
	if (!ret) {
		goto path;
	}

	ret &= bfs_pcheck(setenv("PATH", new_path, true) == 0);
	if (!ret) {
		goto path;
	}

	char *argv[] = {"xspawnee", old_path, NULL};
	pid_t pid = bfs_spawn("xspawnee", &spawn, argv, envp);
	ret &= bfs_pcheck(pid >= 0, "bfs_spawn()");
	if (!ret) {
		goto unset;
	}

	int wstatus;
	ret &= bfs_pcheck(xwaitpid(pid, &wstatus, 0) == pid)
		&& bfs_check(WIFEXITED(wstatus));
	if (ret) {
		int wexit = WEXITSTATUS(wstatus);
		ret &= bfs_check(wexit == EXIT_SUCCESS, "xspawnee: exit(%d)", wexit);
	}

unset:
	if (old_path) {
		ret &= bfs_pcheck(setenv("PATH", old_path, true) == 0);
	} else {
		ret &= bfs_pcheck(unsetenv("PATH") == 0);
	}
path:
	dstrfree(new_path);
	free(old_path);
env:
	for (char **var = envp; *var; ++var) {
		free(*var);
	}
	free(envp);
bin:
	dstrfree(bin);
destroy:
	ret &= bfs_pcheck(bfs_spawn_destroy(&spawn) == 0);
out:
	return ret;
}

/** Check path resolution of non-existent executables. */
static bool check_enoent(bool use_posix) {
	bool ret = true;

	struct bfs_spawn spawn;
	ret &= bfs_pcheck(bfs_spawn_init(&spawn) == 0);
	if (!ret) {
		goto out;
	}

	spawn.flags |= BFS_SPAWN_USE_PATH;
	if (!use_posix) {
		spawn.flags &= ~BFS_SPAWN_USE_POSIX;
	}

	char *argv[] = {"eW6f5RM9Qi", NULL};
	pid_t pid = bfs_spawn("eW6f5RM9Qi", &spawn, argv, NULL);
	ret &= bfs_pcheck(pid < 0 && errno == ENOENT, "bfs_spawn()");

	ret &= bfs_pcheck(bfs_spawn_destroy(&spawn) == 0);
out:
	return ret;
}

static bool check_resolve(void) {
	bool ret = true;
	char *exe;

	exe = bfs_spawn_resolve("sh");
	ret &= bfs_pcheck(exe, "bfs_spawn_resolve('sh')");
	free(exe);

	exe = bfs_spawn_resolve("/bin/sh");
	ret &= bfs_pcheck(exe && strcmp(exe, "/bin/sh") == 0);
	free(exe);

	exe = bfs_spawn_resolve("bin/tests/xspawnee");
	ret &= bfs_pcheck(exe && strcmp(exe, "bin/tests/xspawnee") == 0);
	free(exe);

	ret &= bfs_pcheck(!bfs_spawn_resolve("eW6f5RM9Qi") && errno == ENOENT);

	return ret;
}

bool check_xspawn(void) {
	bool ret = true;

	ret &= check_use_path(true);
	ret &= check_use_path(false);

	ret &= check_enoent(true);
	ret &= check_enoent(false);

	ret &= check_resolve();

	return ret;
}
