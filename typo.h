/*********************************************************************
 * bfs                                                               *
 * Copyright (C) 2016 Tavian Barnes <tavianator@tavianator.com>      *
 *                                                                   *
 * This program is free software. It comes without any warranty, to  *
 * the extent permitted by applicable law. You can redistribute it   *
 * and/or modify it under the terms of the Do What The Fuck You Want *
 * To Public License, Version 2, as published by Sam Hocevar. See    *
 * the COPYING file or http://www.wtfpl.net/ for more details.       *
 *********************************************************************/

#ifndef BFS_TYPO_H
#define BFS_TYPO_H

/**
 * Find the "typo" distance between two strings.
 *
 * @param actual
 *         The actual string typed by the user.
 * @param expected
 *         The expected valid string.
 * @return The distance between the two strings.
 */
int typo_distance(const char *actual, const char *expected);

#endif // BFS_TYPO_H
