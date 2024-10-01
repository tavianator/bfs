// Copyright © Tavian Barnes <tavianator@tavianator.com> and the bfs contributors
// SPDX-License-Identifier: 0BSD

#ifndef BFS_XREGEX_H
#define BFS_XREGEX_H

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
	BFS_REGEX_AWK,
	BFS_REGEX_GNU_AWK,
	BFS_REGEX_EMACS,
	BFS_REGEX_GREP,
	BFS_REGEX_EGREP,
	BFS_REGEX_GNU_FIND,
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
 * @preg (out)
 *         Will hold the compiled regex.
 * @pattern
 *         The regular expression to compile.
 * @type
 *         The regular expression syntax to use.
 * @flags
 *         Regex compilation flags.
 * @return
 *         0 on success, -1 on failure.
 */
int bfs_regcomp(struct bfs_regex **preg, const char *pattern, enum bfs_regex_type type, enum bfs_regcomp_flags flags);

/**
 * Wrapper for regexec().
 *
 * @regex
 *         The regular expression to execute.
 * @str
 *         The string to match against.
 * @flags
 *         Regex execution flags.
 * @return
 *         1 for a match, 0 for no match, -1 on failure.
 */
int bfs_regexec(struct bfs_regex *regex, const char *str, enum bfs_regexec_flags flags);

/**
 * Free a compiled regex.
 */
void bfs_regfree(struct bfs_regex *regex);

/**
 * Get a human-readable regex error message.
 *
 * @regex
 *         The compiled regex.
 * @return
 *         A human-readable description of the error, which should be free()'d.
 */
char *bfs_regerror(const struct bfs_regex *regex);

#endif // BFS_XREGEX_H
