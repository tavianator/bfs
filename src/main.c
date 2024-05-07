// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

/**
 * - main(): the entry point for bfs(1), a breadth-first version of find(1)
 *     - main.c        (this file)
 *
 * - bfs_parse_cmdline(): parses the command line into an expression tree
 *     - ctx.[ch]      (struct bfs_ctx, the overall bfs context)
 *     - expr.h        (declares the expression tree nodes)
 *     - parse.[ch]    (the parser itself)
 *     - opt.[ch]      (the optimizer)
 *
 * - bfs_eval(): runs the expression on every file it sees
 *     - eval.[ch]     (the main evaluation functions)
 *     - exec.[ch]     (implements -exec[dir]/-ok[dir])
 *     - printf.[ch]   (implements -[f]printf)
 *
 * - bftw(): used by bfs_eval() to walk the directory tree(s)
 *     - bftw.[ch]     (an extended version of nftw(3))
 *
 * - Utilities:
 *     - alloc.[ch]    (memory allocation)
 *     - atomic.h      (atomic operations)
 *     - bar.[ch]      (a terminal status bar)
 *     - bit.h         (bit manipulation)
 *     - bfstd.[ch]    (standard library wrappers/polyfills)
 *     - color.[ch]    (for pretty terminal colors)
 *     - prelude.h     (configuration and feature/platform detection)
 *     - diag.[ch]     (formats diagnostic messages)
 *     - dir.[ch]      (a directory API facade)
 *     - dstring.[ch]  (a dynamic string library)
 *     - fsade.[ch]    (a facade over non-standard filesystem features)
 *     - ioq.[ch]      (an async I/O queue)
 *     - list.h        (linked list macros)
 *     - mtab.[ch]     (parses the system's mount table)
 *     - pwcache.[ch]  (a cache for the user/group tables)
 *     - sanity.h      (sanitizer interfaces)
 *     - stat.[ch]     (wraps stat(), or statx() on Linux)
 *     - thread.h      (multi-threading)
 *     - trie.[ch]     (a trie set/map implementation)
 *     - typo.[ch]     (fuzzy matching for typos)
 *     - xregex.[ch]   (regular expression support)
 *     - xspawn.[ch]   (spawns processes)
 *     - xtime.[ch]    (date/time handling utilities)
 */

#include "prelude.h"
#include "bfstd.h"
#include "ctx.h"
#include "diag.h"
#include "eval.h"
#include "exec.h"
#include "expr.h"
#include "parse.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/**
 * Check if a file descriptor is open.
 */
static bool isopen(int fd) {
	return fcntl(fd, F_GETFD) >= 0 || errno != EBADF;
}

/**
 * Open a file and redirect it to a particular descriptor.
 */
static int redirect(int fd, const char *path, int flags) {
	int newfd = open(path, flags);
	if (newfd < 0 || newfd == fd) {
		return newfd;
	}

	int ret = dup2(newfd, fd);
	close_quietly(newfd);
	return ret;
}

/**
 * Make sure the standard streams std{in,out,err} are open.  If they are not,
 * future open() calls may use those file descriptors, and std{in,out,err} will
 * use them unintentionally.
 */
static int open_std_streams(void) {
#ifdef O_PATH
	const int inflags = O_PATH, outflags = O_PATH;
#else
	// These are intentionally backwards so that bfs >&- still fails with EBADF
	const int inflags = O_WRONLY, outflags = O_RDONLY;
#endif

	if (!isopen(STDERR_FILENO) && redirect(STDERR_FILENO, "/dev/null", outflags) < 0) {
		return -1;
	}
	if (!isopen(STDOUT_FILENO) && redirect(STDOUT_FILENO, "/dev/null", outflags) < 0) {
		perror("redirect()");
		return -1;
	}
	if (!isopen(STDIN_FILENO) && redirect(STDIN_FILENO, "/dev/null", inflags) < 0) {
		perror("redirect()");
		return -1;
	}

	return 0;
}

static void find2fd_extract(struct bfs_exprs *exprs, struct bfs_expr *expr) {
	SLIST_INIT(exprs);

	if (expr->eval_fn == eval_and) {
		SLIST_EXTEND(exprs, &expr->children);
	} else {
		SLIST_APPEND(exprs, expr);
	}
}

static void shellesc(dchar **cmdline, const char *str) {
	dstrcat(cmdline, " ");
	dstrescat(cmdline, str, WESC_SHELL | WESC_TTY);
}

/**
 * bfs entry point.
 */
int main(int argc, char *argv[]) {
	// Make sure the standard streams are open
	if (open_std_streams() != 0) {
		return EXIT_FAILURE;
	}

	// Use the system locale instead of "C"
	int locale_err = 0;
	if (!setlocale(LC_ALL, "")) {
		locale_err = errno;
	}

	// Apply the environment's timezone
	tzset();

	// Parse the command line
	struct bfs_ctx *ctx = bfs_parse_cmdline(argc, argv);
	if (!ctx) {
		return EXIT_FAILURE;
	}

	// Warn if setlocale() failed, unless there's no expression to evaluate
	if (locale_err && ctx->warn && ctx->expr) {
		bfs_warning(ctx, "Failed to set locale: %s\n\n", xstrerror(locale_err));
	}

	bool hidden = true;
	if (ctx->exclude->eval_fn == eval_hidden) {
		hidden = false;
	} else if (ctx->exclude->eval_fn != eval_false) {
		bfs_expr_error(ctx, ctx->exclude);
		bfs_error(ctx, "${ex}fd${rs} does not support ${red}-exclude${rs}.\n");
		return EXIT_FAILURE;
	}

	struct bfs_exprs exprs;
	find2fd_extract(&exprs, ctx->expr);

	struct bfs_expr *pattern = NULL;
	struct bfs_expr *type = NULL;
	struct bfs_expr *executable = NULL;
	struct bfs_expr *empty = NULL;
	struct bfs_expr *action = NULL;
	for_slist (struct bfs_expr, expr, &exprs) {
		struct bfs_expr **target = NULL;
		if (expr->eval_fn == eval_name
		    || expr->eval_fn == eval_path
		    || expr->eval_fn == eval_regex) {
			target = &pattern;
		} else if (expr->eval_fn == eval_type) {
			target = &type;
		} else if (expr->eval_fn == eval_access && expr->num == X_OK) {
			target = &executable;
		} else if (expr->eval_fn == eval_empty) {
			target = &empty;
		} else if ((expr->eval_fn == eval_fprint
			    || expr->eval_fn == eval_fprint0
			    || expr->eval_fn == eval_fls)
			   && expr->cfile == ctx->cout) {
			target = &action;
		} else if (expr->eval_fn == eval_exec
			   && !(expr->exec->flags & (BFS_EXEC_CONFIRM | BFS_EXEC_CHDIR))) {
			target = &action;
		}

		if (!target) {
			bfs_expr_error(ctx, expr);
			if (bfs_expr_is_parent(expr)) {
				bfs_error(ctx, "Too complicated to convert to ${ex}fd${rs}.\n");
			} else {
				bfs_error(ctx, "No equivalent ${ex}fd${rs} option.\n");
			}
			return EXIT_FAILURE;
		}

		if (*target) {
			bfs_expr_error(ctx, *target);
			bfs_expr_error(ctx, expr);
			bfs_error(ctx, "${ex}fd${rs} doesn't support both of these at once.\n");
			return EXIT_FAILURE;
		}

		if (action && target != &action) {
			bfs_expr_error(ctx, expr);
			bfs_error(ctx, "${ex}fd${rs} doesn't support this ...\n");
			bfs_expr_error(ctx, *target);
			bfs_error(ctx, "... after this.\n");
			return EXIT_FAILURE;
		}

		*target = expr;
	}

	if (!action) {
		bfs_expr_error(ctx, ctx->expr);
		bfs_error(ctx, "Missing action.\n");
		return EXIT_FAILURE;
	}

	dchar *cmdline = dstralloc(0);

	dstrcat(&cmdline, "fd --no-ignore");

	if (hidden) {
		dstrcat(&cmdline, " --hidden");
	}

	if (ctx->flags & BFTW_POST_ORDER) {
		bfs_error(ctx, "${ex}fd${rs} doesn't support ${blu}-depth${rs}.\n");
		return EXIT_FAILURE;
	}
	if (ctx->flags & BFTW_SORT) {
		bfs_error(ctx, "${ex}fd${rs} doesn't support ${cyn}-s${rs}.\n");
		return EXIT_FAILURE;
	}
	if (ctx->flags & BFTW_FOLLOW_ALL) {
		dstrcat(&cmdline, " --follow");
	}
	if (ctx->flags & (BFTW_SKIP_MOUNTS | BFTW_PRUNE_MOUNTS)) {
		dstrcat(&cmdline, " --one-file-system");
	}

	if (ctx->mindepth == ctx->maxdepth) {
		dstrcatf(&cmdline, " --exact-depth %d", ctx->mindepth);
	} else {
		if (ctx->mindepth > 0) {
			dstrcatf(&cmdline, " --min-depth %d", ctx->mindepth);
		}
		if (ctx->maxdepth < INT_MAX) {
			dstrcatf(&cmdline, " --max-depth %d", ctx->mindepth);
		}
	}

	if (type) {
		unsigned int types = type->num;
		if (types & (1 << BFS_REG)) {
			dstrcat(&cmdline, " --type file");
			types ^= (1 << BFS_REG);
		}
		if (types & (1 << BFS_DIR)) {
			dstrcat(&cmdline, " --type directory");
			types ^= (1 << BFS_DIR);
		}
		if (types & (1 << BFS_LNK)) {
			dstrcat(&cmdline, " --type symlink");
			types ^= (1 << BFS_LNK);
		}
		if (types & (1 << BFS_SOCK)) {
			dstrcat(&cmdline, " --type socket");
			types ^= (1 << BFS_SOCK);
		}
		if (types & (1 << BFS_FIFO)) {
			dstrcat(&cmdline, " --type pipe");
			types ^= (1 << BFS_FIFO);
		}
		if (types) {
			bfs_expr_error(ctx, type);
			bfs_error(ctx, "${ex}fd${rs} doesn't support this type.\n");
			return EXIT_FAILURE;
		}
	}

	if (executable) {
		dstrcat(&cmdline, " --type executable");
	}

	if (empty) {
		dstrcat(&cmdline, " --type empty");
	}

	if (action->eval_fn == eval_fprint0) {
		dstrcat(&cmdline, " --print0");
	} else if (action->eval_fn == eval_fls) {
		dstrcat(&cmdline, " --list-details");
	}

	if (pattern) {
		if (pattern->eval_fn != eval_name) {
			dstrcat(&cmdline, " --full-path");
		}
		if (pattern->eval_fn != eval_regex) {
			dstrcat(&cmdline, " --glob");
		}
		if (pattern->argv[0][1] == 'i') {
			dstrcat(&cmdline, " --ignore-case");
		} else {
			dstrcat(&cmdline, " --case-sensitive");
		}
		shellesc(&cmdline, pattern->argv[1]);
	}

	for (size_t i = 0; i < ctx->npaths; ++i) {
		const char *path = ctx->paths[i];
		if (!pattern || path[0] == '-') {
			dstrcat(&cmdline, " --search-path");
		}
		shellesc(&cmdline, path);
	}

	if (action->eval_fn == eval_exec) {
		struct bfs_exec *execbuf = action->exec;

		dstrcat(&cmdline, " --exec");
		if (execbuf->flags & BFS_EXEC_MULTI) {
			dstrcat(&cmdline, "-batch");
		}

		bool placeholder = false;
		for (size_t i = 0; i < execbuf->tmpl_argc; ++i) {
			const char *arg = execbuf->tmpl_argv[i];
			if (strstr(arg, "{}")) {
				placeholder = true;
				if (i == execbuf->tmpl_argc - 1 && strcmp(arg, "{}") == 0) {
					// fd adds it automatically
					break;
				}
			}
			shellesc(&cmdline, arg);
		}

		if (!placeholder) {
			bfs_expr_error(ctx, action);
			bfs_error(ctx, "${ex}fd${rs} doesn't support ${blu}%s${rs} without a placeholder.\n", action->argv[0]);
			return EXIT_FAILURE;
		}
	}

	printf("%s\n", cmdline);
	return EXIT_SUCCESS;
}
