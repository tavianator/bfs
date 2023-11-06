// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

/**
 * The evaluation functions that implement primary expressions like -name,
 * -print, etc.
 */

#ifndef BFS_EVAL_H
#define BFS_EVAL_H

#include "config.h"

struct bfs_ctx;
struct bfs_expr;

/**
 * Ephemeral state for evaluating an expression.
 */
struct bfs_eval;

/**
 * Expression evaluation function.
 *
 * @param expr
 *         The current expression.
 * @param state
 *         The current evaluation state.
 * @return
 *         The result of the test.
 */
typedef bool bfs_eval_fn(const struct bfs_expr *expr, struct bfs_eval *state);

/**
 * Evaluate the command line.
 *
 * @param ctx
 *         The bfs context to evaluate.
 * @return
 *         EXIT_SUCCESS on success, otherwise on failure.
 */
int bfs_eval(struct bfs_ctx *ctx);

// Predicate evaluation functions

bool eval_true(const struct bfs_expr *expr, struct bfs_eval *state);
bool eval_false(const struct bfs_expr *expr, struct bfs_eval *state);

bool eval_access(const struct bfs_expr *expr, struct bfs_eval *state);
bool eval_acl(const struct bfs_expr *expr, struct bfs_eval *state);
bool eval_capable(const struct bfs_expr *expr, struct bfs_eval *state);
bool eval_perm(const struct bfs_expr *expr, struct bfs_eval *state);
bool eval_xattr(const struct bfs_expr *expr, struct bfs_eval *state);
bool eval_xattrname(const struct bfs_expr *expr, struct bfs_eval *state);

bool eval_newer(const struct bfs_expr *expr, struct bfs_eval *state);
bool eval_time(const struct bfs_expr *expr, struct bfs_eval *state);
bool eval_used(const struct bfs_expr *expr, struct bfs_eval *state);

bool eval_gid(const struct bfs_expr *expr, struct bfs_eval *state);
bool eval_uid(const struct bfs_expr *expr, struct bfs_eval *state);
bool eval_nogroup(const struct bfs_expr *expr, struct bfs_eval *state);
bool eval_nouser(const struct bfs_expr *expr, struct bfs_eval *state);

bool eval_depth(const struct bfs_expr *expr, struct bfs_eval *state);
bool eval_empty(const struct bfs_expr *expr, struct bfs_eval *state);
bool eval_flags(const struct bfs_expr *expr, struct bfs_eval *state);
bool eval_fstype(const struct bfs_expr *expr, struct bfs_eval *state);
bool eval_hidden(const struct bfs_expr *expr, struct bfs_eval *state);
bool eval_inum(const struct bfs_expr *expr, struct bfs_eval *state);
bool eval_links(const struct bfs_expr *expr, struct bfs_eval *state);
bool eval_samefile(const struct bfs_expr *expr, struct bfs_eval *state);
bool eval_size(const struct bfs_expr *expr, struct bfs_eval *state);
bool eval_sparse(const struct bfs_expr *expr, struct bfs_eval *state);
bool eval_type(const struct bfs_expr *expr, struct bfs_eval *state);
bool eval_xtype(const struct bfs_expr *expr, struct bfs_eval *state);

bool eval_lname(const struct bfs_expr *expr, struct bfs_eval *state);
bool eval_name(const struct bfs_expr *expr, struct bfs_eval *state);
bool eval_path(const struct bfs_expr *expr, struct bfs_eval *state);
bool eval_regex(const struct bfs_expr *expr, struct bfs_eval *state);

bool eval_delete(const struct bfs_expr *expr, struct bfs_eval *state);
bool eval_exec(const struct bfs_expr *expr, struct bfs_eval *state);
bool eval_exit(const struct bfs_expr *expr, struct bfs_eval *state);
bool eval_fls(const struct bfs_expr *expr, struct bfs_eval *state);
bool eval_fprint(const struct bfs_expr *expr, struct bfs_eval *state);
bool eval_fprint0(const struct bfs_expr *expr, struct bfs_eval *state);
bool eval_fprintf(const struct bfs_expr *expr, struct bfs_eval *state);
bool eval_fprintx(const struct bfs_expr *expr, struct bfs_eval *state);
bool eval_prune(const struct bfs_expr *expr, struct bfs_eval *state);
bool eval_quit(const struct bfs_expr *expr, struct bfs_eval *state);

// Operator evaluation functions
bool eval_not(const struct bfs_expr *expr, struct bfs_eval *state);
bool eval_and(const struct bfs_expr *expr, struct bfs_eval *state);
bool eval_or(const struct bfs_expr *expr, struct bfs_eval *state);
bool eval_comma(const struct bfs_expr *expr, struct bfs_eval *state);

#endif // BFS_EVAL_H
