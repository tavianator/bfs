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

#if BFS_WITH_ONIGURUMA
#	include <onigposix.h>
#else
#	include <regex.h>
#endif

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
 * Wrapper for regcomp() that supports additional regex types.
 *
 * @param preg
 *         The compiled regex.
 * @param regex
 *         The regular expression to compile.
 * @param cflags
 *         Regex compilation flags.
 * @param type
 *         The regular expression syntax to use.
 * @return
 *         0 on success, or an error code on failure.
 */
int bfs_regcomp(regex_t *preg, const char *regex, int cflags, enum bfs_regex_type type);

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
char *bfs_regerror(int err, const regex_t *regex);

#endif // BFS_REGEX_H
