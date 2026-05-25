// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

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
 * @bar
 *         The status bar to update.
 * @str
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
