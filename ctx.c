/****************************************************************************
 * bfs                                                                      *
 * Copyright (C) 2015-2020 Tavian Barnes <tavianator@tavianator.com>        *
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
#include "darray.h"
#include "diag.h"
#include "expr.h"
#include "mtab.h"
#include "pwcache.h"
#include "trie.h"
#include <assert.h>
#include <errno.h>
#include <stdlib.h>

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

CFILE *bfs_ctx_open(struct bfs_ctx *ctx, const char *path, bool use_color) {
	int error = 0;

	CFILE *cfile = cfopen(path, use_color ? ctx->colors : NULL);
	if (!cfile) {
		error = errno;
		goto out;
	}

	struct bfs_stat sb;
	if (bfs_stat(fileno(cfile->file), NULL, 0, &sb) != 0) {
		error = errno;
		goto out_close;
	}

	bfs_file_id id;
	bfs_stat_id(&sb, &id);

	struct trie_leaf *leaf = trie_insert_mem(&ctx->files, id, sizeof(id));
	if (!leaf) {
		error = errno;
		goto out_close;
	}

	if (leaf->value) {
		struct bfs_ctx_file *ctx_file = leaf->value;
		cfclose(cfile);
		cfile = ctx_file->cfile;
		goto out;
	}

	struct bfs_ctx_file *ctx_file = malloc(sizeof(*ctx_file));
	if (!ctx_file) {
		error = errno;
		trie_remove(&ctx->files, leaf);
		goto out_close;
	}

	ctx_file->cfile = cfile;
	ctx_file->path = path;
	leaf->value = ctx_file;
	++ctx->nfiles;

	goto out;

out_close:
	cfclose(cfile);
	cfile = NULL;
out:
	errno = error;
	return cfile;
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

			if (cfclose(ctx_file->cfile) != 0) {
				if (cerr) {
					bfs_error(ctx, "'%s': %m.\n", ctx_file->path);
				}
				ret = -1;
			}

			free(ctx_file);
			trie_remove(&ctx->files, leaf);
		}
		trie_destroy(&ctx->files);

		if (cout && fflush(cout->file) != 0) {
			if (cerr) {
				bfs_error(ctx, "standard output: %m.\n");
			}
			ret = -1;
		}

		cfclose(cout);
		cfclose(cerr);

		free_colors(ctx->colors);
		darray_free(ctx->paths);
		free(ctx->argv);
		free(ctx);
	}

	return ret;
}
