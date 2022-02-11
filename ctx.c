/****************************************************************************
 * bfs                                                                      *
 * Copyright (C) 2015-2022 Tavian Barnes <tavianator@tavianator.com>        *
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

#include "ctx.h"
#include "color.h"
#include "darray.h"
#include "diag.h"
#include "expr.h"
#include "mtab.h"
#include "pwcache.h"
#include "stat.h"
#include "trie.h"
#include <assert.h>
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

	assert(!"Unrecognized debug flag");
	return "???";
}

struct bfs_ctx *bfs_ctx_new(void) {
	struct bfs_ctx *ctx = malloc(sizeof(*ctx));
	if (!ctx) {
		return NULL;
	}

	ctx->argv = NULL;
	ctx->paths = NULL;
	ctx->expr = NULL;
	ctx->exclude = NULL;

	ctx->mindepth = 0;
	ctx->maxdepth = INT_MAX;
	ctx->flags = BFTW_RECOVER;
	ctx->strategy = BFTW_BFS;
	ctx->optlevel = 3;
	ctx->debug = 0;
	ctx->ignore_races = false;
	ctx->posixly_correct = false;
	ctx->status = false;
	ctx->unique = false;
	ctx->warn = false;
	ctx->xargs_safe = false;

	ctx->colors = NULL;
	ctx->colors_error = 0;
	ctx->cout = NULL;
	ctx->cerr = NULL;

	ctx->users = NULL;
	ctx->users_error = 0;
	ctx->groups = NULL;
	ctx->groups_error = 0;

	ctx->mtab = NULL;
	ctx->mtab_error = 0;

	trie_init(&ctx->files);
	ctx->nfiles = 0;

	struct rlimit rl;
	if (getrlimit(RLIMIT_NOFILE, &rl) == 0) {
		ctx->nofile_soft = rl.rlim_cur;
		ctx->nofile_hard = rl.rlim_max;
	} else {
		ctx->nofile_soft = 1024;
		ctx->nofile_hard = RLIM_INFINITY;
	}

	return ctx;
}

const struct bfs_users *bfs_ctx_users(const struct bfs_ctx *ctx) {
	struct bfs_ctx *mut = (struct bfs_ctx *)ctx;

	if (mut->users_error) {
		errno = mut->users_error;
	} else if (!mut->users) {
		mut->users = bfs_users_parse();
		if (!mut->users) {
			mut->users_error = errno;
		}
	}

	return mut->users;
}

const struct bfs_groups *bfs_ctx_groups(const struct bfs_ctx *ctx) {
	struct bfs_ctx *mut = (struct bfs_ctx *)ctx;

	if (mut->groups_error) {
		errno = mut->groups_error;
	} else if (!mut->groups) {
		mut->groups = bfs_groups_parse();
		if (!mut->groups) {
			mut->groups_error = errno;
		}
	}

	return mut->groups;
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

	leaf->value = ctx_file = malloc(sizeof(*ctx_file));
	if (!ctx_file) {
		trie_remove(&ctx->files, leaf);
		return NULL;
	}

	ctx_file->cfile = cfile;
	ctx_file->path = path;
	++ctx->nfiles;
	return cfile;
}

/** Flush a file and report any errors. */
static int bfs_ctx_flush(CFILE *cfile) {
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
static int bfs_ctx_close(struct bfs_ctx *ctx, struct bfs_ctx_file *ctx_file) {
	CFILE *cfile = ctx_file->cfile;

	if (cfile == ctx->cout) {
		// Will be checked later
		return 0;
	} else if (cfile == ctx->cerr) {
		// Writes to stderr are allowed to fail silently, unless the same file was used by
		// -fprint, -fls, etc.
		if (ctx_file->path) {
			return bfs_ctx_flush(cfile);
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

		free_expr(ctx->expr);
		free_expr(ctx->exclude);

		bfs_mtab_free(ctx->mtab);

		bfs_groups_free(ctx->groups);
		bfs_users_free(ctx->users);

		struct trie_leaf *leaf;
		while ((leaf = trie_first_leaf(&ctx->files))) {
			struct bfs_ctx_file *ctx_file = leaf->value;

			if (bfs_ctx_close(ctx, ctx_file) != 0) {
				if (cerr) {
					bfs_error(ctx, "'%s': %m.\n", ctx_file->path);
				}
				ret = -1;
			}

			free(ctx_file);
			trie_remove(&ctx->files, leaf);
		}
		trie_destroy(&ctx->files);

		if (cout && bfs_ctx_flush(cout) != 0) {
			if (cerr) {
				bfs_error(ctx, "standard output: %m.\n");
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
