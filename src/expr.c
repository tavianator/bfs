// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include "expr.h"
#include "alloc.h"
#include "eval.h"
#include "exec.h"
#include "printf.h"
#include "xregex.h"
#include <stdio.h>
#include <stdlib.h>

struct bfs_expr *bfs_expr_new(bfs_eval_fn *eval_fn, size_t argc, char **argv) {
	struct bfs_expr *expr = ZALLOC(struct bfs_expr);
	if (!expr) {
		perror("zalloc()");
		return NULL;
	}

	expr->eval_fn = eval_fn;
	expr->argc = argc;
	expr->argv = argv;
	expr->probability = 0.5;
	return expr;
}

bool bfs_expr_is_parent(const struct bfs_expr *expr) {
	return expr->eval_fn == eval_and
		|| expr->eval_fn == eval_or
		|| expr->eval_fn == eval_not
		|| expr->eval_fn == eval_comma;
}

bool bfs_expr_never_returns(const struct bfs_expr *expr) {
	// Expressions that never return are vacuously both always true and always false
	return expr->always_true && expr->always_false;
}

void bfs_expr_free(struct bfs_expr *expr) {
	if (!expr) {
		return;
	}

	if (bfs_expr_is_parent(expr)) {
		bfs_expr_free(expr->rhs);
		bfs_expr_free(expr->lhs);
	} else if (expr->eval_fn == eval_exec) {
		bfs_exec_free(expr->exec);
	} else if (expr->eval_fn == eval_fprintf) {
		bfs_printf_free(expr->printf);
	} else if (expr->eval_fn == eval_regex) {
		bfs_regfree(expr->regex);
	}

	free(expr);
}
