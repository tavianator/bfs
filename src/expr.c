// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include "expr.h"

#include "alloc.h"
#include "ctx.h"
#include "diag.h"
#include "eval.h"
#include "exec.h"
#include "list.h"
#include "printf.h"
#include "xregex.h"

#include <string.h>

struct bfs_expr *bfs_expr_new(struct bfs_ctx *ctx, bfs_eval_fn *eval_fn, size_t argc, char **argv, enum bfs_kind kind) {
	bfs_assert(kind != BFS_PATH);

	struct bfs_expr *expr = arena_alloc(&ctx->expr_arena);
	if (!expr) {
		return NULL;
	}

	memset(expr, 0, sizeof(*expr));
	expr->eval_fn = eval_fn;
	expr->argc = argc;
	expr->argv = argv;
	expr->kind = kind;
	expr->probability = 0.5;
	SLIST_PREPEND(&ctx->expr_list, expr, freelist);

	if (bfs_expr_is_parent(expr)) {
		SLIST_INIT(&expr->children);
	}

	return expr;
}

bool bfs_expr_is_parent(const struct bfs_expr *expr) {
	return expr->eval_fn == eval_and
		|| expr->eval_fn == eval_or
		|| expr->eval_fn == eval_not
		|| expr->eval_fn == eval_comma;
}

struct bfs_expr *bfs_expr_children(const struct bfs_expr *expr) {
	if (bfs_expr_is_parent(expr)) {
		return expr->children.head;
	} else {
		return NULL;
	}
}

void bfs_expr_append(struct bfs_expr *expr, struct bfs_expr *child) {
	bfs_assert(bfs_expr_is_parent(expr));

	SLIST_APPEND(&expr->children, child);

	if (!child->pure) {
		expr->pure = false;
	}

	expr->persistent_fds += child->persistent_fds;
	if (expr->ephemeral_fds < child->ephemeral_fds) {
		expr->ephemeral_fds = child->ephemeral_fds;
	}
}

void bfs_expr_extend(struct bfs_expr *expr, struct bfs_exprs *children) {
	drain_slist (struct bfs_expr, child, children) {
		bfs_expr_append(expr, child);
	}
}

bool bfs_expr_never_returns(const struct bfs_expr *expr) {
	// Expressions that never return are vacuously both always true and always false
	return expr->always_true && expr->always_false;
}

void bfs_expr_clear(struct bfs_expr *expr) {
	if (expr->eval_fn == eval_exec) {
		bfs_exec_free(expr->exec);
	} else if (expr->eval_fn == eval_fprintf) {
		bfs_printf_free(expr->printf);
	} else if (expr->eval_fn == eval_regex) {
		bfs_regfree(expr->regex);
	}
}
