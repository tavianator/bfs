/****************************************************************************
 * bfs                                                                      *
 * Copyright (C) 2018 Tavian Barnes <tavianator@tavianator.com>             *
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

#ifndef BFS_SPAWN_H
#define BFS_SPAWN_H

#include <stdbool.h>
#include <signal.h>
#include <sys/types.h>

enum bfs_spawn_flags {
	BFS_SPAWN_USEPATH = 1 << 0,
};

struct bfs_spawn {
	enum bfs_spawn_flags flags;
	struct bfs_spawn_action *actions;
	struct bfs_spawn_action **tail;
};

int bfs_spawn_init(struct bfs_spawn *ctx);
int bfs_spawn_destroy(struct bfs_spawn *ctx);

int bfs_spawn_setflags(struct bfs_spawn *ctx, enum bfs_spawn_flags flags);

int bfs_spawn_addfchdir(struct bfs_spawn *ctx, int fd);

pid_t bfs_spawn(const char *file, const struct bfs_spawn *ctx, char **argv, char **envp);

#endif // BFS_SPAWN_H
