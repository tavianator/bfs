/****************************************************************************
 * bfs                                                                      *
 * Copyright (C) 2020 Tavian Barnes <tavianator@tavianator.com>             *
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
 * A terminal status bar.
 */

#ifndef BFS_BAR_H
#define BFS_BAR_H

/** A terminal status bar. */
struct bfs_bar;

/**
 * Create a terminal status bar.  Only one status bar is supported at a time.
 *
 * @return
 *         A pointer to the new status bar, or NULL on failure.
 */
struct bfs_bar *bfs_bar_show(void);

/**
 * Get the width of the status bar.
 */
unsigned int bfs_bar_width(const struct bfs_bar *bar);

/**
 * Update the status bar message.
 *
 * @param bar
 *         The status bar to update.
 * @param str
 *         The string to display.
 * @return
 *         0 on success, -1 on failure.
 */
int bfs_bar_update(struct bfs_bar *bar, const char *str);

/**
 * Hide the status bar.
 */
void bfs_bar_hide(struct bfs_bar *status);

#endif // BFS_BAR_H
