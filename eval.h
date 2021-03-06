/****************************************************************************
 * bfs                                                                      *
 * Copyright (C) 2015-2018 Tavian Barnes <tavianator@tavianator.com>        *
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

/**
 * The evaluation functions that implement literal expressions like -name,
 * -print, etc.
 */

#ifndef BFS_EVAL_H
#define BFS_EVAL_H

#include <stdbool.h>

struct bfs_ctx;
struct expr;

/**
 * Ephemeral state for evaluating an expression.
 */
struct eval_state;

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
typedef bool eval_fn(const struct expr *expr, struct eval_state *state);

/**
 * Evaluate the command line.
 *
 * @param ctx
 *         The bfs context to evaluate.
 * @return
 *         EXIT_SUCCESS on success, otherwise on failure.
 */
int bfs_eval(const struct bfs_ctx *ctx);

// Predicate evaluation functions

bool eval_true(const struct expr *expr, struct eval_state *state);
bool eval_false(const struct expr *expr, struct eval_state *state);

bool eval_access(const struct expr *expr, struct eval_state *state);
bool eval_acl(const struct expr *expr, struct eval_state *state);
bool eval_capable(const struct expr *expr, struct eval_state *state);
bool eval_perm(const struct expr *expr, struct eval_state *state);
bool eval_xattr(const struct expr *expr, struct eval_state *state);
bool eval_xattrname(const struct expr *expr, struct eval_state *state);

bool eval_newer(const struct expr *expr, struct eval_state *state);
bool eval_time(const struct expr *expr, struct eval_state *state);
bool eval_used(const struct expr *expr, struct eval_state *state);

bool eval_gid(const struct expr *expr, struct eval_state *state);
bool eval_uid(const struct expr *expr, struct eval_state *state);
bool eval_nogroup(const struct expr *expr, struct eval_state *state);
bool eval_nouser(const struct expr *expr, struct eval_state *state);

bool eval_depth(const struct expr *expr, struct eval_state *state);
bool eval_empty(const struct expr *expr, struct eval_state *state);
bool eval_flags(const struct expr *expr, struct eval_state *state);
bool eval_fstype(const struct expr *expr, struct eval_state *state);
bool eval_hidden(const struct expr *expr, struct eval_state *state);
bool eval_inum(const struct expr *expr, struct eval_state *state);
bool eval_links(const struct expr *expr, struct eval_state *state);
bool eval_samefile(const struct expr *expr, struct eval_state *state);
bool eval_size(const struct expr *expr, struct eval_state *state);
bool eval_sparse(const struct expr *expr, struct eval_state *state);
bool eval_type(const struct expr *expr, struct eval_state *state);
bool eval_xtype(const struct expr *expr, struct eval_state *state);

bool eval_lname(const struct expr *expr, struct eval_state *state);
bool eval_name(const struct expr *expr, struct eval_state *state);
bool eval_path(const struct expr *expr, struct eval_state *state);
bool eval_regex(const struct expr *expr, struct eval_state *state);

bool eval_delete(const struct expr *expr, struct eval_state *state);
bool eval_exec(const struct expr *expr, struct eval_state *state);
bool eval_exit(const struct expr *expr, struct eval_state *state);
bool eval_fls(const struct expr *expr, struct eval_state *state);
bool eval_fprint(const struct expr *expr, struct eval_state *state);
bool eval_fprint0(const struct expr *expr, struct eval_state *state);
bool eval_fprintf(const struct expr *expr, struct eval_state *state);
bool eval_fprintx(const struct expr *expr, struct eval_state *state);
bool eval_prune(const struct expr *expr, struct eval_state *state);
bool eval_quit(const struct expr *expr, struct eval_state *state);

// Operator evaluation functions
bool eval_not(const struct expr *expr, struct eval_state *state);
bool eval_and(const struct expr *expr, struct eval_state *state);
bool eval_or(const struct expr *expr, struct eval_state *state);
bool eval_comma(const struct expr *expr, struct eval_state *state);

#endif // BFS_EVAL_H
