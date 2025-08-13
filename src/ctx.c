// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include "ctx.h"

#include "alloc.h"
#include "bfstd.h"
#include "color.h"
#include "diag.h"
#include "expr.h"
#include "list.h"
#include "mtab.h"
#include "pwcache.h"
#include "sighook.h"
#include "stat.h"
#include "trie.h"

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifdef BFS_WITH_LIBGIT2
#include <git2.h>
#endif

struct bfs_ctx *bfs_ctx_new(void) {
	struct bfs_ctx *ctx = ZALLOC(struct bfs_ctx);
	if (!ctx) {
		return NULL;
	}

	SLIST_INIT(&ctx->expr_list);
	ARENA_INIT(&ctx->expr_arena, struct bfs_expr);

	ctx->maxdepth = INT_MAX;
	ctx->flags = BFTW_RECOVER;
	ctx->strategy = BFTW_BFS;
	ctx->optlevel = 3;

	ctx->threads = nproc();
	if (ctx->threads > 8) {
		// Not much speedup after 8 threads
		ctx->threads = 8;
	}

	trie_init(&ctx->files);

	ctx->umask = umask(0);
	umask(ctx->umask);

	if (getrlimit(RLIMIT_NOFILE, &ctx->orig_nofile) != 0) {
		goto fail;
	}
	ctx->cur_nofile = ctx->orig_nofile;
	ctx->raise_nofile = true;

	ctx->users = bfs_users_new();
	if (!ctx->users) {
		goto fail;
	}

	ctx->groups = bfs_groups_new();
	if (!ctx->groups) {
		goto fail;
	}

	if (clock_gettime(CLOCK_REALTIME, &ctx->now) != 0) {
		goto fail;
	}

#ifdef BFS_WITH_LIBGIT2
	git_libgit2_init();
#endif

	return ctx;

fail:
	bfs_ctx_free(ctx);
	return NULL;
}

const struct bfs_mtab *bfs_ctx_mtab(const struct bfs_ctx *ctx) {
	struct bfs_ctx *mut = (struct bfs_ctx *)ctx;

	if (mut->mtab_error) {
		errno = mut->mtab_error;
	} else if (!mut->mtab) {
		mut->mtab = bfs_mtab_parse();
		if (!mut->mtab) {
			mut->mtab_error = errno;
		}
	}

	return mut->mtab;
}

/**
 * An open file tracked by the bfs context.
 */
struct bfs_ctx_file {
	/** The file itself. */
	CFILE *cfile;
	/** The path to the file (for diagnostics). */
	const char *path;
	/** Signal hook to send a reset escape sequence. */
	struct sighook *hook;
	/** Remembers I/O errors, to propagate them to the exit status. */
	int error;
};

/** Call cfreset() on a tracked file. */
static void cfreset_hook(int sig, siginfo_t *info, void *arg) {
	cfreset(arg);
}

CFILE *bfs_ctx_dedup(struct bfs_ctx *ctx, CFILE *cfile, const char *path) {
	struct bfs_stat sb;
	if (bfs_stat(cfile->fd, NULL, 0, &sb) != 0) {
		return NULL;
	}

	bfs_file_id id;
	bfs_stat_id(&sb, &id);

	struct trie_leaf *leaf = trie_insert_mem(&ctx->files, id, sizeof(id));
	if (!leaf) {
		return NULL;
	}

	struct bfs_ctx_file *ctx_file = leaf->value;
	if (ctx_file) {
		ctx_file->path = path;
		return ctx_file->cfile;
	}

	leaf->value = ctx_file = ALLOC(struct bfs_ctx_file);
	if (!ctx_file) {
		goto fail;
	}

	ctx_file->cfile = cfile;
	ctx_file->path = path;
	ctx_file->error = 0;
	ctx_file->hook = NULL;

	if (cfile->colors) {
		ctx_file->hook = atsigexit(cfreset_hook, cfile);
		if (!ctx_file->hook) {
			goto fail;
		}
	}

	if (cfile != ctx->cout && cfile != ctx->cerr) {
		++ctx->nfiles;
	}

	return cfile;

fail:
	trie_remove(&ctx->files, leaf);
	free(ctx_file);
	return NULL;
}

void bfs_ctx_flush(const struct bfs_ctx *ctx) {
	// Before executing anything, flush all open streams.  This ensures that
	// - the user sees everything relevant before an -ok[dir] prompt
	// - output from commands is interleaved consistently with bfs
	// - executed commands can rely on I/O from other bfs actions
	for_trie (leaf, &ctx->files) {
		struct bfs_ctx_file *ctx_file = leaf->value;
		CFILE *cfile = ctx_file->cfile;
		if (fflush(cfile->file) == 0) {
			continue;
		}

		ctx_file->error = errno;
		clearerr(cfile->file);

		const char *path = ctx_file->path;
		if (path) {
			bfs_error(ctx, "%pq: %s.\n", path, errstr());
		} else if (cfile == ctx->cout) {
			bfs_error(ctx, "(standard output): %s.\n", errstr());
		}
	}

	// Flush the user/group caches, in case the executed command edits the
	// user/group tables
	bfs_users_flush(ctx->users);
	bfs_groups_flush(ctx->groups);
}

/** Flush a file and report any errors. */
static int bfs_ctx_fflush(CFILE *cfile) {
	int ret = 0, error = 0;
	if (ferror(cfile->file)) {
		ret = -1;
		error = EIO;
	}
	if (fflush(cfile->file) != 0) {
		ret = -1;
		error = errno;
	}

	errno = error;
	return ret;
}

/** Close a file tracked by the bfs context. */
static int bfs_ctx_fclose(struct bfs_ctx *ctx, struct bfs_ctx_file *ctx_file) {
	CFILE *cfile = ctx_file->cfile;

	// Writes to stderr are allowed to fail silently, unless the same file
	// was used by -fprint, -fls, etc.
	bool silent = cfile == ctx->cerr && !ctx_file->path;
	int ret = 0, error = 0;

	if (ctx_file->error) {
		// An error was previously reported during bfs_ctx_flush()
		ret = -1;
		error = ctx_file->error;
	}

	// Flush the file just before we remove the hook, to maximize the chance
	// we leave the TTY in a good state
	if (bfs_ctx_fflush(cfile) != 0) {
		ret = -1;
		error = errno;
	}

	sigunhook(ctx_file->hook);

	// Close the CFILE, except for stdio streams, which are closed later
	if (cfile != ctx->cout && cfile != ctx->cerr) {
		if (cfclose(cfile) != 0) {
			ret = -1;
			error = errno;
		}
	}

	if (silent) {
		ret = 0;
	}

	if (ret != 0 && ctx->cerr) {
		if (ctx_file->path) {
			bfs_error(ctx, "%pq: %s.\n", ctx_file->path, xstrerror(error));
		} else if (cfile == ctx->cout) {
			bfs_error(ctx, "(standard output): %s.\n", xstrerror(error));
		}
	}

	free(ctx_file);
	return ret;
}

int bfs_ctx_free(struct bfs_ctx *ctx) {
	int ret = 0;

	if (ctx) {
		CFILE *cout = ctx->cout;
		CFILE *cerr = ctx->cerr;

		bfs_mtab_free(ctx->mtab);

		bfs_groups_free(ctx->groups);
		bfs_users_free(ctx->users);

		for_trie (leaf, &ctx->files) {
			struct bfs_ctx_file *ctx_file = leaf->value;
			if (bfs_ctx_fclose(ctx, ctx_file) != 0) {
				ret = -1;
			}
		}
		trie_destroy(&ctx->files);

		cfclose(cout);
		cfclose(cerr);
		free_colors(ctx->colors);

		for_slist (struct bfs_expr, expr, &ctx->expr_list, freelist) {
			bfs_expr_clear(expr);
		}
		arena_destroy(&ctx->expr_arena);

		for (size_t i = 0; i < ctx->npaths; ++i) {
			free((char *)ctx->paths[i]);
		}
		free(ctx->paths);

		free(ctx->kinds);
		free(ctx->argv);

#ifdef BFS_WITH_LIBGIT2
		if (ctx->git_repo) {
			git_repository_free(ctx->git_repo);
		}
		git_libgit2_shutdown();
#endif

		free(ctx);
	}

	return ret;
}

#ifdef BFS_WITH_LIBGIT2
/** Find the git repository for a given path. */
static struct git_repository *bfs_git_repository_open(const struct bfs_ctx *ctx, const char *path) {
	bfs_debug(ctx, DEBUG_SEARCH, "Opening git repository for '%s'\n", path);
	struct git_repository *repo = NULL;
	int error = git_repository_open_ext(&repo, path,
			GIT_REPOSITORY_OPEN_FROM_ENV,
			NULL);
	if (error != 0) {
		if (error != GIT_ENOTFOUND) {
			bfs_warning(ctx, "git_repository_open_ext('%s'): %s.\n",
					path, git_error_last()->message);
		}
		bfs_debug(ctx, DEBUG_SEARCH, "Failed to open git repository for '%s': %s\n", path, git_error_last()->message);
		return NULL;
	}
	bfs_debug(ctx, DEBUG_SEARCH, "Successfully opened git repository for '%s'\n", path);
	return repo;
}

/** Check if a path is ignored by git. */
bool bfs_git_ignored(const struct bfs_ctx *ctx, const struct BFTW *ftwbuf) {
	struct bfs_ctx *mut = (struct bfs_ctx *)ctx;
	if (strcmp(ftwbuf->path, ".") == 0) {
		return false;
	}

	if (!mut->git_repo) {
		mut->git_repo = bfs_git_repository_open(ctx, ftwbuf->root);
		if (!mut->git_repo) {
			return false;
		}
	}

	int ignored = 0;
	const char *git_path = ftwbuf->path;
	if (git_path[0] == '.' && git_path[1] == '/') {
	    git_path += 2; // Slice off "./"
	}
	int error = git_ignore_path_is_ignored(&ignored, mut->git_repo, git_path);
	if (error != 0) {
		bfs_warning(ctx, "%pP: git_ignore_check_path(): %s.\n",
				ftwbuf, git_error_last()->message);
		return false;
	}

	bfs_debug(ctx, DEBUG_SEARCH, "%pP: Ignored by git: %s\n", ftwbuf, ignored ? "true" : "false");
	return ignored;
}
#endif // BFS_WITH_LIBGIT2
