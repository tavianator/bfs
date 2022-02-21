/****************************************************************************
 * bfs                                                                      *
 * Copyright (C) 2022 Tavian Barnes <tavianator@tavianator.com> and bfs     *
 * contributors                                                             *
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

#ifndef BFS_REGEX_H
#define BFS_REGEX_H

#include <stdbool.h>

/**
 * A compiled regular expression.
 */
struct bfs_regex;

/**
 * Regex syntax flavors.
 */
enum bfs_regex_type {
	BFS_REGEX_POSIX_BASIC,
	BFS_REGEX_POSIX_EXTENDED,
	BFS_REGEX_EMACS,
	BFS_REGEX_GREP,
};

/**
 * Regex compilation flags.
 */
enum bfs_regcomp_flags {
	/** Treat the regex case-insensitively. */
	BFS_REGEX_ICASE = 1 << 0,
};

/**
 * Regex execution flags.
 */
enum bfs_regexec_flags {
	/** Only treat matches of the entire string as successful. */
	BFS_REGEX_ANCHOR = 1 << 0,
};

/**
 * Wrapper for regcomp() that supports additional regex types.
 *
 * @param expr
 *         The regular expression to compile.
 * @param type
 *         The regular expression syntax to use.
 * @param flags
 *         Regex compilation flags.
 * @param[out] err
 *         Will hold the error code if compilation fails.
 * @return
 *         The compiled regular expression, or NULL on error.
 */
struct bfs_regex *bfs_regcomp(const char *expr, enum bfs_regex_type type, enum bfs_regcomp_flags flags, int *err);

/**
 * Wrapper for regexec().
 *
 * @param expr
 *         The regular expression to execute.
 * @param str
 *         The string to match against.
 * @param flags
 *         Regex execution flags.
 * @param[out] err
 *         Will hold the error code if execution fails.
 * @return
 *         Whether the regex matched.
 */
bool bfs_regexec(struct bfs_regex *regex, const char *str, enum bfs_regexec_flags flags, int *err);

/**
 * Free a compiled regex.
 */
void bfs_regfree(struct bfs_regex *regex);

/**
 * Dynamically allocate a regex error message.
 *
 * @param err
 *         The error code to stringify.
 * @param regex
 *         The compiled regex, or NULL if compilation failed.
 * @return
 *         A human-readable description of the error, allocated with malloc().
 */
char *bfs_regerror(int err, const struct bfs_regex *regex);

#endif // BFS_REGEX_H
