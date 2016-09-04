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

#include "typo.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

struct coords {
	int x, y;
};

// Assume QWERTY layout for now
static const struct coords key_coords[UCHAR_MAX] = {
	['`']  = { 0,  0},
	['~']  = { 0,  0},
	['1']  = { 3,  0},
	['!']  = { 3,  0},
	['2']  = { 6,  0},
	['@']  = { 6,  0},
	['3']  = { 9,  0},
	['#']  = { 9,  0},
	['4']  = {12,  0},
	['$']  = {12,  0},
	['5']  = {15,  0},
	['%']  = {15,  0},
	['6']  = {18,  0},
	['^']  = {18,  0},
	['7']  = {21,  0},
	['&']  = {21,  0},
	['8']  = {24,  0},
	['*']  = {24,  0},
	['9']  = {27,  0},
	['(']  = {27,  0},
	['0']  = {30,  0},
	[')']  = {30,  0},
	['-']  = {33,  0},
	['_']  = {33,  0},
	['=']  = {36,  0},
	['+']  = {36,  0},

	['\t'] = { 1,  3},
	['q']  = { 4,  3},
	['Q']  = { 4,  3},
	['w']  = { 7,  3},
	['W']  = { 7,  3},
	['e']  = {10,  3},
	['E']  = {10,  3},
	['r']  = {13,  3},
	['R']  = {13,  3},
	['t']  = {16,  3},
	['T']  = {16,  3},
	['y']  = {19,  3},
	['Y']  = {19,  3},
	['u']  = {22,  3},
	['U']  = {22,  3},
	['i']  = {25,  3},
	['I']  = {25,  3},
	['o']  = {28,  3},
	['O']  = {28,  3},
	['p']  = {31,  3},
	['P']  = {31,  3},
	['[']  = {34,  3},
	['{']  = {34,  3},
	[']']  = {37,  3},
	['}']  = {37,  3},
	['\\'] = {40,  3},
	['|']  = {40,  3},

	['a']  = { 5,  6},
	['A']  = { 5,  6},
	['s']  = { 8,  6},
	['S']  = { 8,  6},
	['d']  = {11,  6},
	['D']  = {11,  6},
	['f']  = {14,  6},
	['F']  = {14,  6},
	['g']  = {17,  6},
	['G']  = {17,  6},
	['h']  = {20,  6},
	['H']  = {20,  6},
	['j']  = {23,  6},
	['J']  = {23,  6},
	['k']  = {26,  6},
	['K']  = {26,  6},
	['l']  = {29,  6},
	['L']  = {29,  6},
	[';']  = {32,  6},
	[':']  = {32,  6},
	['\''] = {35,  6},
	['"']  = {35,  6},
	['\n'] = {38,  6},

	['z']  = { 6,  9},
	['Z']  = { 6,  9},
	['x']  = { 9,  9},
	['X']  = { 9,  9},
	['c']  = {12,  9},
	['C']  = {12,  9},
	['v']  = {15,  9},
	['V']  = {15,  9},
	['b']  = {18,  9},
	['B']  = {18,  9},
	['n']  = {21,  9},
	['N']  = {21,  9},
	['m']  = {24,  9},
	['M']  = {24,  9},
	[',']  = {27,  9},
	['<']  = {27,  9},
	['.']  = {30,  9},
	['>']  = {30,  9},
	['/']  = {33,  9},
	['?']  = {33,  9},

	[' ']  = {18, 12},
};

static int char_distance(char a, char b) {
	const struct coords *ac = &key_coords[(unsigned char)a], *bc = &key_coords[(unsigned char)b];
	int dx = abs(ac->x - bc->x);
	int dy = abs(ac->y - bc->y);
	return dx + dy;
}

int typo_distance(const char *actual, const char *expected) {
	// This is the Wagner-Fischer algorithm for Levenshtein distance, using
	// Manhattan distance on the keyboard for individual characters.

	const int insert_cost = 12;

	size_t rows = strlen(actual) + 1;
	size_t cols = strlen(expected) + 1;

	int arr0[cols], arr1[cols];
	int *row0 = arr0, *row1 = arr1;

	for (size_t j = 0; j < cols; ++j) {
		row0[j] = insert_cost * j;
	}

	for (size_t i = 1; i < rows; ++i) {
		row1[0] = row0[0] + insert_cost;

		char a = actual[i - 1];
		for (size_t j = 1; j < cols; ++j) {
			char b = expected[j - 1];
			int cost = row0[j - 1] + char_distance(a, b);
			int del_cost = row0[j] + insert_cost;
			if (del_cost < cost) {
				cost = del_cost;
			}
			int ins_cost = row1[j - 1] + insert_cost;
			if (ins_cost < cost) {
				cost = ins_cost;
			}
			row1[j] = cost;
		}

		int *tmp = row0;
		row0 = row1;
		row1 = tmp;
	}

	return row0[cols - 1];
}
