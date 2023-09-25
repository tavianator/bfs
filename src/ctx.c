// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include "ctx.h"
#include "alloc.h"
#include "color.h"
#include "darray.h"
#include "diag.h"
#include "expr.h"
#include "mtab.h"
#include "pwcache.h"
#include "stat.h"
#include "trie.h"
#include "xtime.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

const char *debug_flag_name(enum debug_flags flag) {
	switch (flag) {
	case DEBUG_COST:
		return "cost";
	case DEBUG_EXEC:
		return "exec";
	case DEBUG_OPT:
		return "opt";
	case DEBUG_RATES:
		return "rates";
	case DEBUG_SEARCH:
		return "search";
	case DEBUG_STAT:
		return "stat";
	case DEBUG_TREE:
		return "tree";

	case DEBUG_ALL:
		break;
	}

	bfs_bug("Unrecognized debug flag");
	return "???";
}

struct bfs_ctx *bfs_ctx_new(void) {
	struct bfs_ctx *ctx = ZALLOC(struct bfs_ctx);
	if (!ctx) {
		return NULL;
	}

	ctx->maxdepth = INT_MAX;
	ctx->flags = BFTW_RECOVER;
	ctx->strategy = BFTW_BFS;
	ctx->optlevel = 3;

	trie_init(&ctx->files);

	struct rlimit rl;
	if (getrlimit(RLIMIT_NOFILE, &rl) != 0) {
		goto fail;
	}
	ctx->nofile_soft = rl.rlim_cur;
	ctx->nofile_hard = rl.rlim_max;

	ctx->users = bfs_users_new();
	if (!ctx->users) {
		goto fail;
	}

	ctx->groups = bfs_groups_new();
	if (!ctx->groups) {
		goto fail;
	}

	if (xgettime(&ctx->now) != 0) {
		goto fail;
	}

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
	/** Remembers I/O errors, to propagate them to the exit status. */
	int error;
};

CFILE *bfs_ctx_dedup(struct bfs_ctx *ctx, CFILE *cfile, const char *path) {
	struct bfs_stat sb;
	if (bfs_stat(fileno(cfile->file), NULL, 0, &sb) != 0) {
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
		trie_remove(&ctx->files, leaf);
		return NULL;
	}

	ctx_file->cfile = cfile;
	ctx_file->path = path;
	ctx_file->error = 0;

	if (cfile != ctx->cout && cfile != ctx->cerr) {
		++ctx->nfiles;
	}

	return cfile;
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
			bfs_error(ctx, "'%s': %m.\n", path);
		} else if (cfile == ctx->cout) {
			bfs_error(ctx, "(standard output): %m.\n");
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

	if (cfile == ctx->cout) {
		// Will be checked later
		return 0;
	} else if (cfile == ctx->cerr) {
		// Writes to stderr are allowed to fail silently, unless the same file was used by
		// -fprint, -fls, etc.
		if (ctx_file->path) {
			return bfs_ctx_fflush(cfile);
		} else {
			return 0;
		}
	}

	int ret = 0, error = 0;
	if (ferror(cfile->file)) {
		ret = -1;
		error = EIO;
	}
	if (cfclose(cfile) != 0) {
		ret = -1;
		error = errno;
	}

	errno = error;
	return ret;
}

int bfs_ctx_free(struct bfs_ctx *ctx) {
	int ret = 0;

	if (ctx) {
		CFILE *cout = ctx->cout;
		CFILE *cerr = ctx->cerr;

		bfs_expr_free(ctx->exclude);
		bfs_expr_free(ctx->expr);

		bfs_mtab_free(ctx->mtab);

		bfs_groups_free(ctx->groups);
		bfs_users_free(ctx->users);

		for_trie (leaf, &ctx->files) {
			struct bfs_ctx_file *ctx_file = leaf->value;

			if (ctx_file->error) {
				// An error was previously reported during bfs_ctx_flush()
				ret = -1;
			}

			if (bfs_ctx_fclose(ctx, ctx_file) != 0) {
				if (cerr) {
					bfs_error(ctx, "'%s': %m.\n", ctx_file->path);
				}
				ret = -1;
			}

			free(ctx_file);
		}
		trie_destroy(&ctx->files);

		if (cout && bfs_ctx_fflush(cout) != 0) {
			if (cerr) {
				bfs_error(ctx, "(standard output): %m.\n");
			}
			ret = -1;
		}

		cfclose(cout);
		cfclose(cerr);

		free_colors(ctx->colors);

		for (size_t i = 0; i < darray_length(ctx->paths); ++i) {
			free((char *)ctx->paths[i]);
		}
		darray_free(ctx->paths);

		free(ctx->argv);
		free(ctx);
	}

	return ret;
}
