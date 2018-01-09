/****************************************************************************
 * bfs                                                                      *
 * Copyright (C) 2015-2017 Tavian Barnes <tavianator@tavianator.com>        *
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

#ifndef BFS_EVAL_H
#define BFS_EVAL_H

#include "expr.h"

// Predicate evaluation functions
bool eval_true(const struct expr *expr, struct eval_state *state);
bool eval_false(const struct expr *expr, struct eval_state *state);

bool eval_access(const struct expr *expr, struct eval_state *state);
bool eval_perm(const struct expr *expr, struct eval_state *state);

bool eval_newer(const struct expr *expr, struct eval_state *state);
bool eval_time(const struct expr *expr, struct eval_state *state);
bool eval_used(const struct expr *expr, struct eval_state *state);

bool eval_gid(const struct expr *expr, struct eval_state *state);
bool eval_uid(const struct expr *expr, struct eval_state *state);
bool eval_nogroup(const struct expr *expr, struct eval_state *state);
bool eval_nouser(const struct expr *expr, struct eval_state *state);

bool eval_depth(const struct expr *expr, struct eval_state *state);
bool eval_empty(const struct expr *expr, struct eval_state *state);
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
bool eval_nohidden(const struct expr *expr, struct eval_state *state);
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
