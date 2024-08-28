// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include "typo.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Assume QWERTY layout for now
static const int8_t key_coords[UCHAR_MAX + 1][3] = {
	['`']  = { 0,  0, 0},
	['~']  = { 0,  0, 1},
	['1']  = { 3,  0, 0},
	['!']  = { 3,  0, 1},
	['2']  = { 6,  0, 0},
	['@']  = { 6,  0, 1},
	['3']  = { 9,  0, 0},
	['#']  = { 9,  0, 1},
	['4']  = {12,  0, 0},
	['$']  = {12,  0, 1},
	['5']  = {15,  0, 0},
	['%']  = {15,  0, 1},
	['6']  = {18,  0, 0},
	['^']  = {18,  0, 1},
	['7']  = {21,  0, 0},
	['&']  = {21,  0, 1},
	['8']  = {24,  0, 0},
	['*']  = {24,  0, 1},
	['9']  = {27,  0, 0},
	['(']  = {27,  0, 1},
	['0']  = {30,  0, 0},
	[')']  = {30,  0, 1},
	['-']  = {33,  0, 0},
	['_']  = {33,  0, 1},
	['=']  = {36,  0, 0},
	['+']  = {36,  0, 1},

	['\t'] = { 1,  3, 0},
	['q']  = { 4,  3, 0},
	['Q']  = { 4,  3, 1},
	['w']  = { 7,  3, 0},
	['W']  = { 7,  3, 1},
	['e']  = {10,  3, 0},
	['E']  = {10,  3, 1},
	['r']  = {13,  3, 0},
	['R']  = {13,  3, 1},
	['t']  = {16,  3, 0},
	['T']  = {16,  3, 1},
	['y']  = {19,  3, 0},
	['Y']  = {19,  3, 1},
	['u']  = {22,  3, 0},
	['U']  = {22,  3, 1},
	['i']  = {25,  3, 0},
	['I']  = {25,  3, 1},
	['o']  = {28,  3, 0},
	['O']  = {28,  3, 1},
	['p']  = {31,  3, 0},
	['P']  = {31,  3, 1},
	['[']  = {34,  3, 0},
	['{']  = {34,  3, 1},
	[']']  = {37,  3, 0},
	['}']  = {37,  3, 1},
	['\\'] = {40,  3, 0},
	['|']  = {40,  3, 1},

	['a']  = { 5,  6, 0},
	['A']  = { 5,  6, 1},
	['s']  = { 8,  6, 0},
	['S']  = { 8,  6, 1},
	['d']  = {11,  6, 0},
	['D']  = {11,  6, 1},
	['f']  = {14,  6, 0},
	['F']  = {14,  6, 1},
	['g']  = {17,  6, 0},
	['G']  = {17,  6, 1},
	['h']  = {20,  6, 0},
	['H']  = {20,  6, 1},
	['j']  = {23,  6, 0},
	['J']  = {23,  6, 1},
	['k']  = {26,  6, 0},
	['K']  = {26,  6, 1},
	['l']  = {29,  6, 0},
	['L']  = {29,  6, 1},
	[';']  = {32,  6, 0},
	[':']  = {32,  6, 1},
	['\''] = {35,  6, 0},
	['"']  = {35,  6, 1},
	['\n'] = {38,  6, 0},

	['z']  = { 6,  9, 0},
	['Z']  = { 6,  9, 1},
	['x']  = { 9,  9, 0},
	['X']  = { 9,  9, 1},
	['c']  = {12,  9, 0},
	['C']  = {12,  9, 1},
	['v']  = {15,  9, 0},
	['V']  = {15,  9, 1},
	['b']  = {18,  9, 0},
	['B']  = {18,  9, 1},
	['n']  = {21,  9, 0},
	['N']  = {21,  9, 1},
	['m']  = {24,  9, 0},
	['M']  = {24,  9, 1},
	[',']  = {27,  9, 0},
	['<']  = {27,  9, 1},
	['.']  = {30,  9, 0},
	['>']  = {30,  9, 1},
	['/']  = {33,  9, 0},
	['?']  = {33,  9, 1},

	[' ']  = {18, 12, 0},
};

static int char_distance(char a, char b) {
	const int8_t *ac = key_coords[(unsigned char)a], *bc = key_coords[(unsigned char)b];
	int ret = 0;
	for (int i = 0; i < 3; ++i) {
		ret += abs(ac[i] - bc[i]);
	}
	return ret;
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
