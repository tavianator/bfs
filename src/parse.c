// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

/**
 * The command line parser.  Expressions are parsed by recursive descent, with a
 * grammar described in the comments of the parse_*() functions.  The parser
 * also accepts flags and paths at any point in the expression, by treating
 * flags like always-true options, and skipping over paths wherever they appear.
 */

#include "parse.h"
#include "alloc.h"
#include "bfstd.h"
#include "bftw.h"
#include "color.h"
#include "config.h"
#include "ctx.h"
#include "diag.h"
#include "dir.h"
#include "eval.h"
#include "exec.h"
#include "expr.h"
#include "fsade.h"
#include "list.h"
#include "opt.h"
#include "printf.h"
#include "pwcache.h"
#include "sanity.h"
#include "stat.h"
#include "typo.h"
#include "xregex.h"
#include "xspawn.h"
#include "xtime.h"
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <grp.h>
#include <limits.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

// Strings printed by -D tree for "fake" expressions
static char *fake_and_arg = "-and";
static char *fake_hidden_arg = "-hidden";
static char *fake_or_arg = "-or";
static char *fake_print_arg = "-print";
static char *fake_true_arg = "-true";

/**
 * Color use flags.
 */
enum use_color {
	COLOR_NEVER,
	COLOR_AUTO,
	COLOR_ALWAYS,
};

/**
 * Command line parser state.
 */
struct bfs_parser {
	/** The command line being constructed. */
	struct bfs_ctx *ctx;
	/** The command line arguments being parsed. */
	char **argv;
	/** The name of this program. */
	const char *command;

	/** The current regex flags to use. */
	enum bfs_regex_type regex_type;

	/** Whether stdout is a terminal. */
	bool stdout_tty;
	/** Whether this session is interactive (stdin and stderr are each a terminal). */
	bool interactive;
	/** Whether -color or -nocolor has been passed. */
	enum use_color use_color;
	/** Whether a -print action is implied. */
	bool implicit_print;
	/** Whether the default root "." should be used. */
	bool implicit_root;
	/** Whether the expression has started. */
	bool expr_started;
	/** Whether an information option like -help or -version was passed. */
	bool just_info;
	/** Whether we are currently parsing an -exclude expression. */
	bool excluding;

	/** The last non-path argument. */
	char **last_arg;
	/** A "-depth"-type argument, if any. */
	char **depth_arg;
	/** A "-prune" argument, if any. */
	char **prune_arg;
	/** A "-mount" argument, if any. */
	char **mount_arg;
	/** An "-xdev" argument, if any. */
	char **xdev_arg;
	/** A "-files0-from -" argument, if any. */
	char **files0_stdin_arg;
	/** An "-ok"-type expression, if any. */
	const struct bfs_expr *ok_expr;

	/** The current time (maybe modified by -daystart). */
	struct timespec now;
};

/**
 * Possible token types.
 */
enum token_type {
	/** A flag. */
	T_FLAG,
	/** A root path. */
	T_PATH,
	/** An option. */
	T_OPTION,
	/** A test. */
	T_TEST,
	/** An action. */
	T_ACTION,
	/** An operator. */
	T_OPERATOR,
};

/**
 * Print a low-level error message during parsing.
 */
static void parse_perror(const struct bfs_parser *parser, const char *str) {
	bfs_perror(parser->ctx, str);
}

/** Initialize an empty highlighted range. */
static void init_highlight(const struct bfs_ctx *ctx, bool *args) {
	for (size_t i = 0; i < ctx->argc; ++i) {
		args[i] = false;
	}
}

/** Highlight a range of command line arguments. */
static void highlight_args(const struct bfs_ctx *ctx, char **argv, size_t argc, bool *args) {
	size_t i = argv - ctx->argv;
	for (size_t j = 0; j < argc; ++j) {
		bfs_assert(i + j < ctx->argc);
		args[i + j] = true;
	}
}

/**
 * Print an error message during parsing.
 */
attr(printf(2, 3))
static void parse_error(const struct bfs_parser *parser, const char *format, ...) {
	int error = errno;
	const struct bfs_ctx *ctx = parser->ctx;

	bool highlight[ctx->argc];
	init_highlight(ctx, highlight);
	highlight_args(ctx, parser->argv, 1, highlight);
	bfs_argv_error(ctx, highlight);

	va_list args;
	va_start(args, format);
	errno = error;
	bfs_verror(parser->ctx, format, args);
	va_end(args);
}

/**
 * Print an error about some command line arguments.
 */
attr(printf(4, 5))
static void parse_argv_error(const struct bfs_parser *parser, char **argv, size_t argc, const char *format, ...) {
	int error = errno;
	const struct bfs_ctx *ctx = parser->ctx;

	bool highlight[ctx->argc];
	init_highlight(ctx, highlight);
	highlight_args(ctx, argv, argc, highlight);
	bfs_argv_error(ctx, highlight);

	va_list args;
	va_start(args, format);
	errno = error;
	bfs_verror(ctx, format, args);
	va_end(args);
}

/**
 * Print an error about conflicting command line arguments.
 */
attr(printf(6, 7))
static void parse_conflict_error(const struct bfs_parser *parser, char **argv1, size_t argc1, char **argv2, size_t argc2, const char *format, ...) {
	int error = errno;
	const struct bfs_ctx *ctx = parser->ctx;

	bool highlight[ctx->argc];
	init_highlight(ctx, highlight);
	highlight_args(ctx, argv1, argc1, highlight);
	highlight_args(ctx, argv2, argc2, highlight);
	bfs_argv_error(ctx, highlight);

	va_list args;
	va_start(args, format);
	errno = error;
	bfs_verror(ctx, format, args);
	va_end(args);
}

/**
 * Print an error about an expression.
 */
attr(printf(3, 4))
static void parse_expr_error(const struct bfs_parser *parser, const struct bfs_expr *expr, const char *format, ...) {
	int error = errno;
	const struct bfs_ctx *ctx = parser->ctx;

	bfs_expr_error(ctx, expr);

	va_list args;
	va_start(args, format);
	errno = error;
	bfs_verror(ctx, format, args);
	va_end(args);
}

/**
 * Print a warning message during parsing.
 */
attr(printf(2, 3))
static bool parse_warning(const struct bfs_parser *parser, const char *format, ...) {
	int error = errno;
	const struct bfs_ctx *ctx = parser->ctx;

	bool highlight[ctx->argc];
	init_highlight(ctx, highlight);
	highlight_args(ctx, parser->argv, 1, highlight);
	if (!bfs_argv_warning(ctx, highlight)) {
		return false;
	}

	va_list args;
	va_start(args, format);
	errno = error;
	bool ret = bfs_vwarning(parser->ctx, format, args);
	va_end(args);
	return ret;
}

/**
 * Print a warning about conflicting command line arguments.
 */
attr(printf(6, 7))
static bool parse_conflict_warning(const struct bfs_parser *parser, char **argv1, size_t argc1, char **argv2, size_t argc2, const char *format, ...) {
	int error = errno;
	const struct bfs_ctx *ctx = parser->ctx;

	bool highlight[ctx->argc];
	init_highlight(ctx, highlight);
	highlight_args(ctx, argv1, argc1, highlight);
	highlight_args(ctx, argv2, argc2, highlight);
	if (!bfs_argv_warning(ctx, highlight)) {
		return false;
	}

	va_list args;
	va_start(args, format);
	errno = error;
	bool ret = bfs_vwarning(ctx, format, args);
	va_end(args);
	return ret;
}

/**
 * Print a warning about an expression.
 */
attr(printf(3, 4))
static bool parse_expr_warning(const struct bfs_parser *parser, const struct bfs_expr *expr, const char *format, ...) {
	int error = errno;
	const struct bfs_ctx *ctx = parser->ctx;

	if (!bfs_expr_warning(ctx, expr)) {
		return false;
	}

	va_list args;
	va_start(args, format);
	errno = error;
	bool ret = bfs_vwarning(ctx, format, args);
	va_end(args);
	return ret;
}

/**
 * Allocate a new expression.
 */
static struct bfs_expr *parse_new_expr(const struct bfs_parser *parser, bfs_eval_fn *eval_fn, size_t argc, char **argv) {
	struct bfs_expr *expr = bfs_expr_new(parser->ctx, eval_fn, argc, argv);
	if (!expr) {
		parse_perror(parser, "bfs_expr_new()");
	}
	return expr;
}

/**
 * Create a new unary expression.
 */
static struct bfs_expr *new_unary_expr(const struct bfs_parser *parser, bfs_eval_fn *eval_fn, struct bfs_expr *rhs, char **argv) {
	struct bfs_expr *expr = parse_new_expr(parser, eval_fn, 1, argv);
	if (!expr) {
		return NULL;
	}

	bfs_assert(bfs_expr_is_parent(expr));
	bfs_expr_append(expr, rhs);
	return expr;
}

/**
 * Create a new binary expression.
 */
static struct bfs_expr *new_binary_expr(const struct bfs_parser *parser, bfs_eval_fn *eval_fn, struct bfs_expr *lhs, struct bfs_expr *rhs, char **argv) {
	struct bfs_expr *expr = parse_new_expr(parser, eval_fn, 1, argv);
	if (!expr) {
		return NULL;
	}

	bfs_assert(bfs_expr_is_parent(expr));
	bfs_expr_append(expr, lhs);
	bfs_expr_append(expr, rhs);
	return expr;
}

/**
 * Fill in a "-print"-type expression.
 */
static void init_print_expr(struct bfs_parser *parser, struct bfs_expr *expr) {
	expr->cfile = parser->ctx->cout;
	expr->path = NULL;
}

/**
 * Open a file for an expression.
 */
static int expr_open(struct bfs_parser *parser, struct bfs_expr *expr, const char *path) {
	struct bfs_ctx *ctx = parser->ctx;

	FILE *file = NULL;
	CFILE *cfile = NULL;

	file = xfopen(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC);
	if (!file) {
		goto fail;
	}

	cfile = cfwrap(file, parser->use_color ? ctx->colors : NULL, true);
	if (!cfile) {
		goto fail;
	}

	CFILE *dedup = bfs_ctx_dedup(ctx, cfile, path);
	if (!dedup) {
		goto fail;
	}

	if (dedup != cfile) {
		cfclose(cfile);
	}

	expr->cfile = dedup;
	expr->path = path;
	return 0;

fail:
	parse_expr_error(parser, expr, "%m.\n");
	if (cfile) {
		cfclose(cfile);
	} else if (file) {
		fclose(file);
	}
	return -1;
}

/**
 * Invoke bfs_stat() on an argument.
 */
static int stat_arg(const struct bfs_parser *parser, char **arg, struct bfs_stat *sb) {
	const struct bfs_ctx *ctx = parser->ctx;

	bool follow = ctx->flags & (BFTW_FOLLOW_ROOTS | BFTW_FOLLOW_ALL);
	enum bfs_stat_flags flags = follow ? BFS_STAT_TRYFOLLOW : BFS_STAT_NOFOLLOW;

	int ret = bfs_stat(AT_FDCWD, *arg, flags, sb);
	if (ret != 0) {
		parse_argv_error(parser, arg, 1, "%m.\n");
	}
	return ret;
}

/**
 * Parse the expression specified on the command line.
 */
static struct bfs_expr *parse_expr(struct bfs_parser *parser);

/**
 * Advance by a single token.
 */
static char **parser_advance(struct bfs_parser *parser, enum token_type type, size_t argc) {
	if (type != T_FLAG && type != T_PATH) {
		parser->expr_started = true;
	}

	if (type != T_PATH) {
		parser->last_arg = parser->argv;
	}

	char **argv = parser->argv;
	parser->argv += argc;
	return argv;
}

/**
 * Parse a root path.
 */
static int parse_root(struct bfs_parser *parser, const char *path) {
	struct bfs_ctx *ctx = parser->ctx;
	const char **root = RESERVE(const char *, &ctx->paths, &ctx->npaths);
	if (!root) {
		parse_perror(parser, "RESERVE()");
		return -1;
	}

	*root = strdup(path);
	if (!*root) {
		--ctx->npaths;
		parse_perror(parser, "strdup()");
		return -1;
	}

	parser->implicit_root = false;
	return 0;
}

/**
 * While parsing an expression, skip any paths and add them to ctx->paths.
 */
static int skip_paths(struct bfs_parser *parser) {
	while (true) {
		const char *arg = parser->argv[0];
		if (!arg) {
			return 0;
		}

		if (arg[0] == '-') {
			if (strcmp(arg, "--") == 0) {
				// find uses -- to separate flags from the rest
				// of the command line.  We allow mixing flags
				// and paths/predicates, so we just ignore --.
				parser_advance(parser, T_FLAG, 1);
				continue;
			}
			if (strcmp(arg, "-") != 0) {
				// - by itself is a file name.  Anything else
				// starting with - is a flag/predicate.
				return 0;
			}
		}

		// By POSIX, these are always options
		if (strcmp(arg, "(") == 0 || strcmp(arg, "!") == 0) {
			return 0;
		}

		if (parser->expr_started) {
			// By POSIX, these can be paths.  We only treat them as
			// such at the beginning of the command line.
			if (strcmp(arg, ")") == 0 || strcmp(arg, ",") == 0) {
				return 0;
			}
		}

		if (parser->excluding) {
			parse_warning(parser, "This path will not be excluded.  Use a test like ${blu}-name${rs} or ${blu}-path${rs}\n");
			bfs_warning(parser->ctx, "within ${red}-exclude${rs} to exclude matching files.\n\n");
		}

		if (parse_root(parser, arg) != 0) {
			return -1;
		}

		parser_advance(parser, T_PATH, 1);
	}
}

/** Integer parsing flags. */
enum int_flags {
	IF_BASE_MASK   = 0x03F,
	IF_INT         = 0x040,
	IF_LONG        = 0x080,
	IF_LONG_LONG   = 0x0C0,
	IF_SIZE_MASK   = 0x0C0,
	IF_UNSIGNED    = 0x100,
	IF_PARTIAL_OK  = 0x200,
	IF_QUIET       = 0x400,
};

/**
 * Parse an integer.
 */
static const char *parse_int(const struct bfs_parser *parser, char **arg, const char *str, void *result, enum int_flags flags) {
	// strtoll() skips leading spaces, but we want to reject them
	if (xisspace(str[0])) {
		goto bad;
	}

	int base = flags & IF_BASE_MASK;
	if (base == 0) {
		base = 10;
	}

	char *endptr;
	errno = 0;
	long long value = strtoll(str, &endptr, base);
	if (errno != 0) {
		if (errno == ERANGE) {
			goto range;
		} else {
			goto bad;
		}
	}

	// https://github.com/llvm/llvm-project/issues/64946
	sanitize_init(&endptr);

	if (endptr == str) {
		goto bad;
	}

	if (!(flags & IF_PARTIAL_OK) && *endptr != '\0') {
		goto bad;
	}

	if ((flags & IF_UNSIGNED) && value < 0) {
		goto negative;
	}

	switch (flags & IF_SIZE_MASK) {
	case IF_INT:
		if (value < INT_MIN || value > INT_MAX) {
			goto range;
		}
		*(int *)result = value;
		break;

	case IF_LONG:
		if (value < LONG_MIN || value > LONG_MAX) {
			goto range;
		}
		*(long *)result = value;
		break;

	case IF_LONG_LONG:
		*(long long *)result = value;
		break;

	default:
		bfs_bug("Invalid int size");
		goto bad;
	}

	return endptr;

bad:
	if (!(flags & IF_QUIET)) {
		parse_argv_error(parser, arg, 1, "${bld}%pq${rs} is not a valid integer.\n", str);
	}
	return NULL;

negative:
	if (!(flags & IF_QUIET)) {
		parse_argv_error(parser, arg, 1, "Negative integer ${bld}%pq${rs} is not allowed here.\n", str);
	}
	return NULL;

range:
	if (!(flags & IF_QUIET)) {
		parse_argv_error(parser, arg, 1, "${bld}%pq${rs} is too large an integer.\n", str);
	}
	return NULL;
}

/**
 * Parse an integer and a comparison flag.
 */
static const char *parse_icmp(const struct bfs_parser *parser, struct bfs_expr *expr, enum int_flags flags) {
	char **arg = &expr->argv[1];
	const char *str = *arg;
	switch (str[0]) {
	case '-':
		expr->int_cmp = BFS_INT_LESS;
		++str;
		break;
	case '+':
		expr->int_cmp = BFS_INT_GREATER;
		++str;
		break;
	default:
		expr->int_cmp = BFS_INT_EQUAL;
		break;
	}

	return parse_int(parser, arg, str, &expr->num, flags | IF_LONG_LONG | IF_UNSIGNED);
}

/**
 * Check if a string could be an integer comparison.
 */
static bool looks_like_icmp(const char *str) {
	int i;

	// One +/- for the comparison flag, one for the sign
	for (i = 0; i < 2; ++i) {
		if (str[i] != '-' && str[i] != '+') {
			break;
		}
	}

	return str[i] >= '0' && str[i] <= '9';
}

/**
 * Parse a single flag.
 */
static struct bfs_expr *parse_flag(struct bfs_parser *parser, size_t argc) {
	char **argv = parser_advance(parser, T_FLAG, argc);
	return parse_new_expr(parser, eval_true, argc, argv);
}

/**
 * Parse a flag that doesn't take a value.
 */
static struct bfs_expr *parse_nullary_flag(struct bfs_parser *parser) {
	return parse_flag(parser, 1);
}

/**
 * Parse a flag that takes a value.
 */
static struct bfs_expr *parse_unary_flag(struct bfs_parser *parser) {
	const char *arg = parser->argv[0];
	const char *value = parser->argv[1];
	if (!value) {
		parse_error(parser, "${cyn}%s${rs} needs a value.\n", arg);
		return NULL;
	}

	return parse_flag(parser, 2);
}

/**
 * Parse a single option.
 */
static struct bfs_expr *parse_option(struct bfs_parser *parser, size_t argc) {
	char **argv = parser_advance(parser, T_OPTION, argc);
	return parse_new_expr(parser, eval_true, argc, argv);
}

/**
 * Parse an option that doesn't take a value.
 */
static struct bfs_expr *parse_nullary_option(struct bfs_parser *parser) {
	return parse_option(parser, 1);
}

/**
 * Parse an option that takes a value.
 */
static struct bfs_expr *parse_unary_option(struct bfs_parser *parser) {
	const char *arg = parser->argv[0];
	const char *value = parser->argv[1];
	if (!value) {
		parse_error(parser, "${blu}%s${rs} needs a value.\n", arg);
		return NULL;
	}

	return parse_option(parser, 2);
}

/**
 * Parse a single test.
 */
static struct bfs_expr *parse_test(struct bfs_parser *parser, bfs_eval_fn *eval_fn, size_t argc) {
	char **argv = parser_advance(parser, T_TEST, argc);
	return parse_new_expr(parser, eval_fn, argc, argv);
}

/**
 * Parse a test that doesn't take a value.
 */
static struct bfs_expr *parse_nullary_test(struct bfs_parser *parser, bfs_eval_fn *eval_fn) {
	return parse_test(parser, eval_fn, 1);
}

/**
 * Parse a test that takes a value.
 */
static struct bfs_expr *parse_unary_test(struct bfs_parser *parser, bfs_eval_fn *eval_fn) {
	const char *arg = parser->argv[0];
	const char *value = parser->argv[1];
	if (!value) {
		parse_error(parser, "${blu}%s${rs} needs a value.\n", arg);
		return NULL;
	}

	return parse_test(parser, eval_fn, 2);
}

/**
 * Parse a single action.
 */
static struct bfs_expr *parse_action(struct bfs_parser *parser, bfs_eval_fn *eval_fn, size_t argc) {
	char **argv = parser_advance(parser, T_ACTION, argc);

	if (parser->excluding) {
		parse_argv_error(parser, argv, argc, "This action is not supported within ${red}-exclude${rs}.\n");
		return NULL;
	}

	if (eval_fn != eval_prune && eval_fn != eval_quit) {
		parser->implicit_print = false;
	}

	return parse_new_expr(parser, eval_fn, argc, argv);
}

/**
 * Parse an action that takes no arguments.
 */
static struct bfs_expr *parse_nullary_action(struct bfs_parser *parser, bfs_eval_fn *eval_fn) {
	return parse_action(parser, eval_fn, 1);
}

/**
 * Parse an action that takes one argument.
 */
static struct bfs_expr *parse_unary_action(struct bfs_parser *parser, bfs_eval_fn *eval_fn) {
	const char *arg = parser->argv[0];
	const char *value = parser->argv[1];
	if (!value) {
		parse_error(parser, "${blu}%s${rs} needs a value.\n", arg);
		return NULL;
	}

	return parse_action(parser, eval_fn, 2);
}

/**
 * Parse a test expression with integer data and a comparison flag.
 */
static struct bfs_expr *parse_test_icmp(struct bfs_parser *parser, bfs_eval_fn *eval_fn) {
	struct bfs_expr *expr = parse_unary_test(parser, eval_fn);
	if (!expr) {
		return NULL;
	}

	if (!parse_icmp(parser, expr, 0)) {
		return NULL;
	}

	return expr;
}

/**
 * Print usage information for -D.
 */
static void debug_help(CFILE *cfile) {
	cfprintf(cfile, "Supported debug flags:\n\n");

	cfprintf(cfile, "  ${bld}help${rs}:   This message.\n");
	cfprintf(cfile, "  ${bld}cost${rs}:   Show cost estimates.\n");
	cfprintf(cfile, "  ${bld}exec${rs}:   Print executed command details.\n");
	cfprintf(cfile, "  ${bld}opt${rs}:    Print optimization details.\n");
	cfprintf(cfile, "  ${bld}rates${rs}:  Print predicate success rates.\n");
	cfprintf(cfile, "  ${bld}search${rs}: Trace the filesystem traversal.\n");
	cfprintf(cfile, "  ${bld}stat${rs}:   Trace all stat() calls.\n");
	cfprintf(cfile, "  ${bld}tree${rs}:   Print the parse tree.\n");
	cfprintf(cfile, "  ${bld}all${rs}:    All debug flags at once.\n");
}

/** Check if a substring matches a debug flag. */
static bool parse_debug_flag(const char *flag, size_t len, const char *expected) {
	if (len == strlen(expected)) {
		return strncmp(flag, expected, len) == 0;
	} else {
		return false;
	}
}

/**
 * Parse -D FLAG.
 */
static struct bfs_expr *parse_debug(struct bfs_parser *parser, int arg1, int arg2) {
	struct bfs_ctx *ctx = parser->ctx;

	struct bfs_expr *expr = parse_unary_flag(parser);
	if (!expr) {
		cfprintf(ctx->cerr, "\n");
		debug_help(ctx->cerr);
		return NULL;
	}

	bool unrecognized = false;

	for (const char *flag = expr->argv[1], *next; flag; flag = next) {
		size_t len = strcspn(flag, ",");
		if (flag[len]) {
			next = flag + len + 1;
		} else {
			next = NULL;
		}

		if (parse_debug_flag(flag, len, "help")) {
			debug_help(ctx->cout);
			parser->just_info = true;
			return NULL;
		} else if (parse_debug_flag(flag, len, "all")) {
			ctx->debug = DEBUG_ALL;
			continue;
		}

		enum debug_flags i;
		for (i = 1; DEBUG_ALL & i; i <<= 1) {
			const char *name = debug_flag_name(i);
			if (parse_debug_flag(flag, len, name)) {
				break;
			}
		}

		if (DEBUG_ALL & i) {
			ctx->debug |= i;
		} else {
			if (parse_expr_warning(parser, expr, "Unrecognized debug flag ${bld}")) {
				fwrite(flag, 1, len, stderr);
				cfprintf(ctx->cerr, "${rs}.\n\n");
				unrecognized = true;
			}
		}
	}

	if (unrecognized) {
		debug_help(ctx->cerr);
		cfprintf(ctx->cerr, "\n");
	}

	return expr;
}

/**
 * Parse -On.
 */
static struct bfs_expr *parse_optlevel(struct bfs_parser *parser, int arg1, int arg2) {
	struct bfs_expr *expr = parse_nullary_flag(parser);
	if (!expr) {
		return NULL;
	}

	int *optlevel = &parser->ctx->optlevel;

	if (strcmp(expr->argv[0], "-Ofast") == 0) {
		*optlevel = 4;
	} else if (!parse_int(parser, expr->argv, expr->argv[0] + 2, optlevel, IF_INT | IF_UNSIGNED)) {
		return NULL;
	}

	if (*optlevel > 4) {
		parse_expr_warning(parser, expr, "${cyn}-O${bld}%s${rs} is the same as ${cyn}-O${bld}4${rs}.\n\n", expr->argv[0] + 2);
	}

	return expr;
}

/**
 * Parse -[PHL], -follow.
 */
static struct bfs_expr *parse_follow(struct bfs_parser *parser, int flags, int option) {
	struct bfs_ctx *ctx = parser->ctx;
	ctx->flags &= ~(BFTW_FOLLOW_ROOTS | BFTW_FOLLOW_ALL);
	ctx->flags |= flags;
	if (option) {
		return parse_nullary_option(parser);
	} else {
		return parse_nullary_flag(parser);
	}
}

/**
 * Parse -X.
 */
static struct bfs_expr *parse_xargs_safe(struct bfs_parser *parser, int arg1, int arg2) {
	parser->ctx->xargs_safe = true;
	return parse_nullary_flag(parser);
}

/**
 * Parse -executable, -readable, -writable
 */
static struct bfs_expr *parse_access(struct bfs_parser *parser, int flag, int arg2) {
	struct bfs_expr *expr = parse_nullary_test(parser, eval_access);
	if (expr) {
		expr->num = flag;
	}
	return expr;
}

/**
 * Parse -acl.
 */
static struct bfs_expr *parse_acl(struct bfs_parser *parser, int flag, int arg2) {
#if BFS_CAN_CHECK_ACL
	return parse_nullary_test(parser, eval_acl);
#else
	parse_error(parser, "Missing platform support.\n");
	return NULL;
#endif
}

/**
 * Parse -[aBcm]?newer.
 */
static struct bfs_expr *parse_newer(struct bfs_parser *parser, int field, int arg2) {
	struct bfs_expr *expr = parse_unary_test(parser, eval_newer);
	if (!expr) {
		return NULL;
	}

	struct bfs_stat sb;
	if (stat_arg(parser, &expr->argv[1], &sb) != 0) {
		return NULL;
	}

	expr->reftime = sb.mtime;
	expr->stat_field = field;
	return expr;
}

/**
 * Parse -[aBcm]min.
 */
static struct bfs_expr *parse_min(struct bfs_parser *parser, int field, int arg2) {
	struct bfs_expr *expr = parse_test_icmp(parser, eval_time);
	if (!expr) {
		return NULL;
	}

	expr->reftime = parser->now;
	expr->stat_field = field;
	expr->time_unit = BFS_MINUTES;
	return expr;
}

/**
 * Parse -[aBcm]time.
 */
static struct bfs_expr *parse_time(struct bfs_parser *parser, int field, int arg2) {
	struct bfs_expr *expr = parse_unary_test(parser, eval_time);
	if (!expr) {
		return NULL;
	}

	expr->reftime = parser->now;
	expr->stat_field = field;

	const char *tail = parse_icmp(parser, expr, IF_PARTIAL_OK);
	if (!tail) {
		return NULL;
	}

	if (!*tail) {
		expr->time_unit = BFS_DAYS;
		return expr;
	}

	unsigned long long time = expr->num;
	expr->num = 0;

	while (true) {
		switch (*tail) {
		case 'w':
			time *= 7;
			fallthru;
		case 'd':
			time *= 24;
			fallthru;
		case 'h':
			time *= 60;
			fallthru;
		case 'm':
			time *= 60;
			fallthru;
		case 's':
			break;
		default:
			parse_expr_error(parser, expr, "Unknown time unit ${bld}%c${rs}.\n", *tail);
			return NULL;
		}

		expr->num += time;

		if (!*++tail) {
			break;
		}

		tail = parse_int(parser, &expr->argv[1], tail, &time, IF_PARTIAL_OK | IF_LONG_LONG | IF_UNSIGNED);
		if (!tail) {
			return NULL;
		}
		if (!*tail) {
			parse_expr_error(parser, expr, "Missing time unit.\n");
			return NULL;
		}
	}

	expr->time_unit = BFS_SECONDS;
	return expr;
}

/**
 * Parse -capable.
 */
static struct bfs_expr *parse_capable(struct bfs_parser *parser, int flag, int arg2) {
#if BFS_CAN_CHECK_CAPABILITIES
	return parse_nullary_test(parser, eval_capable);
#else
	parse_error(parser, "Missing platform support.\n");
	return NULL;
#endif
}

/**
 * Parse -(no)?color.
 */
static struct bfs_expr *parse_color(struct bfs_parser *parser, int color, int arg2) {
	struct bfs_expr *expr = parse_nullary_option(parser);
	if (!expr) {
		return NULL;
	}

	struct bfs_ctx *ctx = parser->ctx;
	struct colors *colors = ctx->colors;

	if (color) {
		if (!colors) {
			parse_expr_error(parser, expr, "Error parsing $$LS_COLORS: %s.\n", xstrerror(ctx->colors_error));
			return NULL;
		}

		parser->use_color = COLOR_ALWAYS;
		ctx->cout->colors = colors;
		ctx->cerr->colors = colors;
	} else {
		parser->use_color = COLOR_NEVER;
		ctx->cout->colors = NULL;
		ctx->cerr->colors = NULL;
	}

	return expr;
}

/**
 * Parse -{false,true}.
 */
static struct bfs_expr *parse_const(struct bfs_parser *parser, int value, int arg2) {
	return parse_nullary_test(parser, value ? eval_true : eval_false);
}

/**
 * Parse -daystart.
 */
static struct bfs_expr *parse_daystart(struct bfs_parser *parser, int arg1, int arg2) {
	struct tm tm;
	if (!localtime_r(&parser->now.tv_sec, &tm)) {
		parse_perror(parser, "localtime_r()");
		return NULL;
	}

	if (tm.tm_hour || tm.tm_min || tm.tm_sec || parser->now.tv_nsec) {
		++tm.tm_mday;
	}
	tm.tm_hour = 0;
	tm.tm_min = 0;
	tm.tm_sec = 0;

	time_t time;
	if (xmktime(&tm, &time) != 0) {
		parse_perror(parser, "xmktime()");
		return NULL;
	}

	parser->now.tv_sec = time;
	parser->now.tv_nsec = 0;

	return parse_nullary_option(parser);
}

/**
 * Parse -delete.
 */
static struct bfs_expr *parse_delete(struct bfs_parser *parser, int arg1, int arg2) {
	parser->ctx->flags |= BFTW_POST_ORDER;
	parser->depth_arg = parser->argv;
	return parse_nullary_action(parser, eval_delete);
}

/**
 * Parse -d.
 */
static struct bfs_expr *parse_depth(struct bfs_parser *parser, int arg1, int arg2) {
	parser->ctx->flags |= BFTW_POST_ORDER;
	parser->depth_arg = parser->argv;
	return parse_nullary_flag(parser);
}

/**
 * Parse -depth [N].
 */
static struct bfs_expr *parse_depth_n(struct bfs_parser *parser, int arg1, int arg2) {
	const char *arg = parser->argv[1];
	if (arg && looks_like_icmp(arg)) {
		return parse_test_icmp(parser, eval_depth);
	} else {
		return parse_depth(parser, arg1, arg2);
	}
}

/**
 * Parse -{min,max}depth N.
 */
static struct bfs_expr *parse_depth_limit(struct bfs_parser *parser, int is_min, int arg2) {
	struct bfs_expr *expr = parse_unary_option(parser);
	if (!expr) {
		return NULL;
	}

	struct bfs_ctx *ctx = parser->ctx;
	int *depth = is_min ? &ctx->mindepth : &ctx->maxdepth;
	char **arg = &expr->argv[1];
	if (!parse_int(parser, arg, *arg, depth, IF_INT | IF_UNSIGNED)) {
		return NULL;
	}

	return expr;
}

/**
 * Parse -empty.
 */
static struct bfs_expr *parse_empty(struct bfs_parser *parser, int arg1, int arg2) {
	struct bfs_expr *expr = parse_nullary_test(parser, eval_empty);
	if (expr) {
		// For opendir()
		expr->ephemeral_fds = 1;
	}
	return expr;
}

/**
 * Parse -exec(dir)?/-ok(dir)?.
 */
static struct bfs_expr *parse_exec(struct bfs_parser *parser, int flags, int arg2) {
	struct bfs_exec *execbuf = bfs_exec_parse(parser->ctx, parser->argv, flags);
	if (!execbuf) {
		return NULL;
	}

	struct bfs_expr *expr = parse_action(parser, eval_exec, execbuf->tmpl_argc + 2);
	if (!expr) {
		bfs_exec_free(execbuf);
		return NULL;
	}

	expr->exec = execbuf;

	// For pipe() in bfs_spawn()
	expr->ephemeral_fds = 2;

	if (execbuf->flags & BFS_EXEC_CHDIR) {
		// Check for relative paths in $PATH
		const char *path = getenv("PATH");
		while (path) {
			if (*path != '/') {
				size_t len = strcspn(path, ":");
				char *comp = strndup(path, len);
				if (comp) {
					parse_expr_error(parser, expr,
						"This action would be unsafe, since ${bld}$$PATH${rs} contains the relative path ${bld}%pq${rs}\n", comp);
					free(comp);
				} else {
					parse_perror(parser, "strndup()");
				}
				return NULL;
			}

			path = strchr(path, ':');
			if (path) {
				++path;
			}
		}

		// To dup() the parent directory
		if (execbuf->flags & BFS_EXEC_MULTI) {
			++expr->persistent_fds;
		} else {
			++expr->ephemeral_fds;
		}
	}

	if (execbuf->flags & BFS_EXEC_CONFIRM) {
		parser->ok_expr = expr;
	}

	return expr;
}

/**
 * Parse -exit [STATUS].
 */
static struct bfs_expr *parse_exit(struct bfs_parser *parser, int arg1, int arg2) {
	size_t argc = 1;
	const char *value = parser->argv[1];

	int status = EXIT_SUCCESS;
	if (value && parse_int(parser, NULL, value, &status, IF_INT | IF_UNSIGNED | IF_QUIET)) {
		argc = 2;
	}

	struct bfs_expr *expr = parse_action(parser, eval_exit, argc);
	if (expr) {
		expr->num = status;
	}
	return expr;
}

/**
 * Parse -f PATH.
 */
static struct bfs_expr *parse_f(struct bfs_parser *parser, int arg1, int arg2) {
	struct bfs_expr *expr = parse_unary_flag(parser);
	if (!expr) {
		return NULL;
	}

	if (parse_root(parser, expr->argv[1]) != 0) {
		return NULL;
	}

	return expr;
}

/**
 * Parse -files0-from PATH.
 */
static struct bfs_expr *parse_files0_from(struct bfs_parser *parser, int arg1, int arg2) {
	struct bfs_expr *expr = parse_unary_option(parser);
	if (!expr) {
		return NULL;
	}

	const char *from = expr->argv[1];

	FILE *file;
	if (strcmp(from, "-") == 0) {
		file = stdin;
	} else {
		file = xfopen(from, O_RDONLY | O_CLOEXEC);
	}
	if (!file) {
		parse_expr_error(parser, expr, "%m.\n");
		return NULL;
	}

	while (true) {
		char *path = xgetdelim(file, '\0');
		if (!path) {
			if (errno) {
				goto fail;
			} else {
				break;
			}
		}

		int ret = parse_root(parser, path);
		free(path);
		if (ret != 0) {
			goto fail;
		}
	}

	if (file == stdin) {
		parser->files0_stdin_arg = expr->argv;
	} else {
		fclose(file);
	}

	parser->implicit_root = false;
	return expr;

fail:
	if (file != stdin) {
		fclose(file);
	}
	return NULL;
}

/**
 * Parse -flags FLAGS.
 */
static struct bfs_expr *parse_flags(struct bfs_parser *parser, int arg1, int arg2) {
	struct bfs_expr *expr = parse_unary_test(parser, eval_flags);
	if (!expr) {
		return NULL;
	}

	const char *flags = expr->argv[1];
	switch (flags[0]) {
	case '-':
		expr->flags_cmp = BFS_MODE_ALL;
		++flags;
		break;
	case '+':
		expr->flags_cmp = BFS_MODE_ANY;
		++flags;
		break;
	default:
		expr->flags_cmp = BFS_MODE_EQUAL;
		break;
	}

	if (xstrtofflags(&flags, &expr->set_flags, &expr->clear_flags) != 0) {
		if (errno == ENOTSUP) {
			parse_expr_error(parser, expr, "Missing platform support.\n");
		} else {
			parse_expr_error(parser, expr, "Invalid flags.\n");
		}
		return NULL;
	}

	return expr;
}

/**
 * Parse -fls FILE.
 */
static struct bfs_expr *parse_fls(struct bfs_parser *parser, int arg1, int arg2) {
	struct bfs_expr *expr = parse_unary_action(parser, eval_fls);
	if (!expr) {
		return NULL;
	}

	if (expr_open(parser, expr, expr->argv[1]) != 0) {
		return NULL;
	}

	return expr;
}

/**
 * Parse -fprint FILE.
 */
static struct bfs_expr *parse_fprint(struct bfs_parser *parser, int arg1, int arg2) {
	struct bfs_expr *expr = parse_unary_action(parser, eval_fprint);
	if (!expr) {
		return NULL;
	}

	if (expr_open(parser, expr, expr->argv[1]) != 0) {
		return NULL;
	}

	return expr;
}

/**
 * Parse -fprint0 FILE.
 */
static struct bfs_expr *parse_fprint0(struct bfs_parser *parser, int arg1, int arg2) {
	struct bfs_expr *expr = parse_unary_action(parser, eval_fprint0);
	if (!expr) {
		return NULL;
	}

	if (expr_open(parser, expr, expr->argv[1]) != 0) {
		return NULL;
	}

	return expr;
}

/**
 * Parse -fprintf FILE FORMAT.
 */
static struct bfs_expr *parse_fprintf(struct bfs_parser *parser, int arg1, int arg2) {
	const char *arg = parser->argv[0];

	const char *file = parser->argv[1];
	if (!file) {
		parse_error(parser, "${blu}%s${rs} needs a file.\n", arg);
		return NULL;
	}

	const char *format = parser->argv[2];
	if (!format) {
		parse_error(parser, "${blu}%s${rs} needs a format string.\n", arg);
		return NULL;
	}

	struct bfs_expr *expr = parse_action(parser, eval_fprintf, 3);
	if (!expr) {
		return NULL;
	}

	if (expr_open(parser, expr, file) != 0) {
		return NULL;
	}

	if (bfs_printf_parse(parser->ctx, expr, format) != 0) {
		return NULL;
	}

	return expr;
}

/**
 * Parse -fstype TYPE.
 */
static struct bfs_expr *parse_fstype(struct bfs_parser *parser, int arg1, int arg2) {
	struct bfs_expr *expr = parse_unary_test(parser, eval_fstype);
	if (!expr) {
		return NULL;
	}

	if (!bfs_ctx_mtab(parser->ctx)) {
		parse_expr_error(parser, expr, "Couldn't parse the mount table: %m.\n");
		return NULL;
	}

	return expr;
}

/**
 * Parse -gid/-group.
 */
static struct bfs_expr *parse_group(struct bfs_parser *parser, int arg1, int arg2) {
	struct bfs_expr *expr = parse_unary_test(parser, eval_gid);
	if (!expr) {
		return NULL;
	}

	const struct group *grp = bfs_getgrnam(parser->ctx->groups, expr->argv[1]);
	if (grp) {
		expr->num = grp->gr_gid;
		expr->int_cmp = BFS_INT_EQUAL;
	} else if (looks_like_icmp(expr->argv[1])) {
		if (!parse_icmp(parser, expr, 0)) {
			return NULL;
		}
	} else if (errno) {
		parse_expr_error(parser, expr, "%m.\n");
		return NULL;
	} else {
		parse_expr_error(parser, expr, "No such group.\n");
		return NULL;
	}

	return expr;
}

/**
 * Parse -unique.
 */
static struct bfs_expr *parse_unique(struct bfs_parser *parser, int arg1, int arg2) {
	parser->ctx->unique = true;
	return parse_nullary_option(parser);
}

/**
 * Parse -used N.
 */
static struct bfs_expr *parse_used(struct bfs_parser *parser, int arg1, int arg2) {
	return parse_test_icmp(parser, eval_used);
}

/**
 * Parse -uid/-user.
 */
static struct bfs_expr *parse_user(struct bfs_parser *parser, int arg1, int arg2) {
	struct bfs_expr *expr = parse_unary_test(parser, eval_uid);
	if (!expr) {
		return NULL;
	}

	const struct passwd *pwd = bfs_getpwnam(parser->ctx->users, expr->argv[1]);
	if (pwd) {
		expr->num = pwd->pw_uid;
		expr->int_cmp = BFS_INT_EQUAL;
	} else if (looks_like_icmp(expr->argv[1])) {
		if (!parse_icmp(parser, expr, 0)) {
			return NULL;
		}
	} else if (errno) {
		parse_expr_error(parser, expr, "%m.\n");
		return NULL;
	} else {
		parse_expr_error(parser, expr, "No such user.\n");
		return NULL;
	}

	return expr;
}

/**
 * Parse -hidden.
 */
static struct bfs_expr *parse_hidden(struct bfs_parser *parser, int arg1, int arg2) {
	return parse_nullary_test(parser, eval_hidden);
}

/**
 * Parse -(no)?ignore_readdir_race.
 */
static struct bfs_expr *parse_ignore_races(struct bfs_parser *parser, int ignore, int arg2) {
	parser->ctx->ignore_races = ignore;
	return parse_nullary_option(parser);
}

/**
 * Parse -inum N.
 */
static struct bfs_expr *parse_inum(struct bfs_parser *parser, int arg1, int arg2) {
	return parse_test_icmp(parser, eval_inum);
}

/**
 * Parse -j<n>.
 */
static struct bfs_expr *parse_jobs(struct bfs_parser *parser, int arg1, int arg2) {
	struct bfs_expr *expr = parse_nullary_flag(parser);
	if (!expr) {
		return NULL;
	}

	unsigned int n;
	if (!parse_int(parser, expr->argv, expr->argv[0] + 2, &n, IF_INT | IF_UNSIGNED)) {
		return NULL;
	}

	if (n == 0) {
		parse_expr_error(parser, expr, "${bld}0${rs} is not enough threads.\n");
		return NULL;
	}

	parser->ctx->threads = n;
	return expr;
}

/**
 * Parse -links N.
 */
static struct bfs_expr *parse_links(struct bfs_parser *parser, int arg1, int arg2) {
	return parse_test_icmp(parser, eval_links);
}

/**
 * Parse -ls.
 */
static struct bfs_expr *parse_ls(struct bfs_parser *parser, int arg1, int arg2) {
	struct bfs_expr *expr = parse_nullary_action(parser, eval_fls);
	if (!expr) {
		return NULL;
	}

	init_print_expr(parser, expr);
	return expr;
}

/**
 * Parse -mount.
 */
static struct bfs_expr *parse_mount(struct bfs_parser *parser, int arg1, int arg2) {
	struct bfs_expr *expr = parse_nullary_option(parser);
	if (!expr) {
		return NULL;
	}

	parse_expr_warning(parser, expr, "In the future, ${blu}%s${rs} will skip mount points entirely, unlike\n", expr->argv[0]);
	bfs_warning(parser->ctx, "${blu}-xdev${rs}, due to http://austingroupbugs.net/view.php?id=1133.\n\n");

	parser->ctx->flags |= BFTW_PRUNE_MOUNTS;
	parser->mount_arg = expr->argv;
	return expr;
}

/**
 * Common code for fnmatch() tests.
 */
static struct bfs_expr *parse_fnmatch(const struct bfs_parser *parser, struct bfs_expr *expr, bool casefold) {
	if (!expr) {
		return NULL;
	}

	expr->pattern = expr->argv[1];

	if (casefold) {
#ifdef FNM_CASEFOLD
		expr->fnm_flags = FNM_CASEFOLD;
#else
		parse_expr_error(parser, expr, "Missing platform support.\n");
		return NULL;
#endif
	} else {
		expr->fnm_flags = 0;
	}

	// POSIX says, about fnmatch():
	//
	//     If pattern ends with an unescaped <backslash>, fnmatch() shall
	//     return a non-zero value (indicating either no match or an error).
	//
	// But not all implementations obey this, so check for it ourselves.
	size_t i, len = strlen(expr->pattern);
	for (i = 0; i < len; ++i) {
		if (expr->pattern[len - i - 1] != '\\') {
			break;
		}
	}
	if (i % 2 != 0) {
		parse_expr_warning(parser, expr, "Unescaped trailing backslash.\n\n");
		expr->eval_fn = eval_false;
		return expr;
	}

	// strcmp() can be much faster than fnmatch() since it doesn't have to
	// parse the pattern, so special-case patterns with no wildcards.
	//
	//     https://pubs.opengroup.org/onlinepubs/9699919799/utilities/V3_chap02.html#tag_18_13_01
	expr->literal = strcspn(expr->pattern, "?*\\[") == len;

	return expr;
}

/**
 * Parse -i?name.
 */
static struct bfs_expr *parse_name(struct bfs_parser *parser, int casefold, int arg2) {
	struct bfs_expr *expr = parse_unary_test(parser, eval_name);
	return parse_fnmatch(parser, expr, casefold);
}

/**
 * Parse -i?path, -i?wholename.
 */
static struct bfs_expr *parse_path(struct bfs_parser *parser, int casefold, int arg2) {
	struct bfs_expr *expr = parse_unary_test(parser, eval_path);
	return parse_fnmatch(parser, expr, casefold);
}

/**
 * Parse -i?lname.
 */
static struct bfs_expr *parse_lname(struct bfs_parser *parser, int casefold, int arg2) {
	struct bfs_expr *expr = parse_unary_test(parser, eval_lname);
	return parse_fnmatch(parser, expr, casefold);
}

/** Get the bfs_stat_field for X/Y in -newerXY. */
static enum bfs_stat_field parse_newerxy_field(char c) {
	switch (c) {
	case 'a':
		return BFS_STAT_ATIME;
	case 'B':
		return BFS_STAT_BTIME;
	case 'c':
		return BFS_STAT_CTIME;
	case 'm':
		return BFS_STAT_MTIME;
	default:
		return 0;
	}
}

/** Parse an explicit reference timestamp for -newerXt and -*since. */
static int parse_reftime(const struct bfs_parser *parser, struct bfs_expr *expr) {
	if (xgetdate(expr->argv[1], &expr->reftime) == 0) {
		return 0;
	} else if (errno != EINVAL) {
		parse_expr_error(parser, expr, "%m.\n");
		return -1;
	}

	parse_expr_error(parser, expr, "Invalid timestamp.\n\n");
	fprintf(stderr, "Supported timestamp formats are ISO 8601-like, e.g.\n\n");

	struct tm tm;
	if (!localtime_r(&parser->now.tv_sec, &tm)) {
		parse_perror(parser, "localtime_r()");
		return -1;
	}

	int year = tm.tm_year + 1900;
	int month = tm.tm_mon + 1;
	fprintf(stderr, "  - %04d-%02d-%02d\n", year, month, tm.tm_mday);
	fprintf(stderr, "  - %04d-%02d-%02dT%02d:%02d:%02d\n", year, month, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);

#if __FreeBSD__
	int gmtoff = tm.tm_gmtoff;
#else
	int gmtoff = -timezone;
#endif
	int tz_hour = gmtoff / 3600;
	int tz_min = (labs(gmtoff) / 60) % 60;
	fprintf(stderr, "  - %04d-%02d-%02dT%02d:%02d:%02d%+03d:%02d\n",
		year, month, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, tz_hour, tz_min);

	if (!gmtime_r(&parser->now.tv_sec, &tm)) {
		parse_perror(parser, "gmtime_r()");
		return -1;
	}

	year = tm.tm_year + 1900;
	month = tm.tm_mon + 1;
	fprintf(stderr, "  - %04d-%02d-%02dT%02d:%02d:%02dZ\n", year, month, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);

	return -1;
}

/**
 * Parse -newerXY.
 */
static struct bfs_expr *parse_newerxy(struct bfs_parser *parser, int arg1, int arg2) {
	const char *arg = parser->argv[0];
	if (strlen(arg) != 8) {
		parse_error(parser, "Expected ${blu}-newer${bld}XY${rs}; found ${blu}-newer${bld}%pq${rs}.\n", arg + 6);
		return NULL;
	}

	struct bfs_expr *expr = parse_unary_test(parser, eval_newer);
	if (!expr) {
		return NULL;
	}

	expr->stat_field = parse_newerxy_field(arg[6]);
	if (!expr->stat_field) {
		parse_expr_error(parser, expr,
			"For ${blu}-newer${bld}XY${rs}, ${bld}X${rs} should be ${bld}a${rs}, ${bld}c${rs}, ${bld}m${rs}, or ${bld}B${rs}, not ${err}%c${rs}.\n",
			arg[6]);
		return NULL;
	}

	if (arg[7] == 't') {
		if (parse_reftime(parser, expr) != 0) {
			return NULL;
		}
	} else {
		enum bfs_stat_field field = parse_newerxy_field(arg[7]);
		if (!field) {
			parse_expr_error(parser, expr,
				"For ${blu}-newer${bld}XY${rs}, ${bld}Y${rs} should be ${bld}a${rs}, ${bld}c${rs}, ${bld}m${rs}, ${bld}B${rs}, or ${bld}t${rs}, not ${err}%c${rs}.\n",
				arg[7]);
			return NULL;
		}

		struct bfs_stat sb;
		if (stat_arg(parser, &expr->argv[1], &sb) != 0) {
			return NULL;
		}

		const struct timespec *reftime = bfs_stat_time(&sb, field);
		if (!reftime) {
			parse_expr_error(parser, expr, "Couldn't get file %s.\n", bfs_stat_field_name(field));
			return NULL;
		}

		expr->reftime = *reftime;
	}

	return expr;
}

/**
 * Parse -nogroup.
 */
static struct bfs_expr *parse_nogroup(struct bfs_parser *parser, int arg1, int arg2) {
	struct bfs_expr *expr = parse_nullary_test(parser, eval_nogroup);
	if (expr) {
		// Who knows how many FDs getgrgid_r() needs?
		expr->ephemeral_fds = 3;
	}
	return expr;
}

/**
 * Parse -nohidden.
 */
static struct bfs_expr *parse_nohidden(struct bfs_parser *parser, int arg1, int arg2) {
	struct bfs_expr *hidden = parse_new_expr(parser, eval_hidden, 1, &fake_hidden_arg);
	if (!hidden) {
		return NULL;
	}

	bfs_expr_append(parser->ctx->exclude, hidden);
	return parse_nullary_option(parser);
}

/**
 * Parse -noleaf.
 */
static struct bfs_expr *parse_noleaf(struct bfs_parser *parser, int arg1, int arg2) {
	parse_warning(parser, "${ex}%s${rs} does not apply the optimization that ${blu}%s${rs} inhibits.\n\n",
		BFS_COMMAND, parser->argv[0]);
	return parse_nullary_option(parser);
}

/**
 * Parse -nouser.
 */
static struct bfs_expr *parse_nouser(struct bfs_parser *parser, int arg1, int arg2) {
	struct bfs_expr *expr = parse_nullary_test(parser, eval_nouser);
	if (expr) {
		// Who knows how many FDs getpwuid_r() needs?
		expr->ephemeral_fds = 3;
	}
	return expr;
}

/**
 * Parse a permission mode like chmod(1).
 */
static int parse_mode(const struct bfs_parser *parser, const char *mode, struct bfs_expr *expr) {
	if (mode[0] >= '0' && mode[0] <= '9') {
		unsigned int parsed;
		if (!parse_int(parser, NULL, mode, &parsed, 8 | IF_INT | IF_UNSIGNED | IF_QUIET)) {
			goto fail;
		}
		if (parsed > 07777) {
			goto fail;
		}

		expr->file_mode = parsed;
		expr->dir_mode = parsed;
		return 0;
	}

	expr->file_mode = 0;
	expr->dir_mode = 0;

	// Parse the same grammar as chmod(1), which looks like this:
	//
	// MODE : CLAUSE ["," CLAUSE]*
	//
	// CLAUSE : WHO* ACTION+
	//
	// WHO : "u" | "g" | "o" | "a"
	//
	// ACTION : OP PERM*
	//        | OP PERMCOPY
	//
	// OP : "+" | "-" | "="
	//
	// PERM : "r" | "w" | "x" | "X" | "s" | "t"
	//
	// PERMCOPY : "u" | "g" | "o"

	// Parser machine parser
	enum {
		MODE_CLAUSE,
		MODE_WHO,
		MODE_ACTION,
		MODE_ACTION_APPLY,
		MODE_OP,
		MODE_PERM,
	} mparser = MODE_CLAUSE;

	enum {
		MODE_PLUS,
		MODE_MINUS,
		MODE_EQUALS,
	} op uninit(MODE_EQUALS);

	mode_t who uninit(0);
	mode_t file_change uninit(0);
	mode_t dir_change uninit(0);

	const char *i = mode;
	while (true) {
		switch (mparser) {
		case MODE_CLAUSE:
			who = 0;
			mparser = MODE_WHO;
			fallthru;

		case MODE_WHO:
			switch (*i) {
			case 'u':
				who |= 0700;
				break;
			case 'g':
				who |= 0070;
				break;
			case 'o':
				who |= 0007;
				break;
			case 'a':
				who |= 0777;
				break;
			default:
				mparser = MODE_ACTION;
				continue;
			}
			break;

		case MODE_ACTION_APPLY:
			switch (op) {
			case MODE_EQUALS:
				expr->file_mode &= ~who;
				expr->dir_mode &= ~who;
				fallthru;
			case MODE_PLUS:
				expr->file_mode |= file_change;
				expr->dir_mode |= dir_change;
				break;
			case MODE_MINUS:
				expr->file_mode &= ~file_change;
				expr->dir_mode &= ~dir_change;
				break;
			}
			fallthru;

		case MODE_ACTION:
			if (who == 0) {
				who = 0777;
			}

			switch (*i) {
			case '+':
				op = MODE_PLUS;
				mparser = MODE_OP;
				break;
			case '-':
				op = MODE_MINUS;
				mparser = MODE_OP;
				break;
			case '=':
				op = MODE_EQUALS;
				mparser = MODE_OP;
				break;

			case ',':
				if (mparser == MODE_ACTION_APPLY) {
					mparser = MODE_CLAUSE;
				} else {
					goto fail;
				}
				break;

			case '\0':
				if (mparser == MODE_ACTION_APPLY) {
					goto done;
				} else {
					goto fail;
				}

			default:
				goto fail;
			}
			break;

		case MODE_OP:
			switch (*i) {
			case 'u':
				file_change = (expr->file_mode >> 6) & 07;
				dir_change = (expr->dir_mode >> 6) & 07;
				break;
			case 'g':
				file_change = (expr->file_mode >> 3) & 07;
				dir_change = (expr->dir_mode >> 3) & 07;
				break;
			case 'o':
				file_change = expr->file_mode & 07;
				dir_change = expr->dir_mode & 07;
				break;

			default:
				file_change = 0;
				dir_change = 0;
				mparser = MODE_PERM;
				continue;
			}

			file_change |= (file_change << 6) | (file_change << 3);
			file_change &= who;
			dir_change |= (dir_change << 6) | (dir_change << 3);
			dir_change &= who;
			mparser = MODE_ACTION_APPLY;
			break;

		case MODE_PERM:
			switch (*i) {
			case 'r':
				file_change |= who & 0444;
				dir_change |= who & 0444;
				break;
			case 'w':
				file_change |= who & 0222;
				dir_change |= who & 0222;
				break;
			case 'x':
				file_change |= who & 0111;
				fallthru;
			case 'X':
				dir_change |= who & 0111;
				break;
			case 's':
				if (who & 0700) {
					file_change |= S_ISUID;
					dir_change |= S_ISUID;
				}
				if (who & 0070) {
					file_change |= S_ISGID;
					dir_change |= S_ISGID;
				}
				break;
			case 't':
				if (who & 0007) {
					file_change |= S_ISVTX;
					dir_change |= S_ISVTX;
				}
				break;
			default:
				mparser = MODE_ACTION_APPLY;
				continue;
			}
			break;
		}

		++i;
	}

done:
	return 0;

fail:
	parse_expr_error(parser, expr, "Invalid mode.\n");
	return -1;
}

/**
 * Parse -perm MODE.
 */
static struct bfs_expr *parse_perm(struct bfs_parser *parser, int field, int arg2) {
	struct bfs_expr *expr = parse_unary_test(parser, eval_perm);
	if (!expr) {
		return NULL;
	}

	const char *mode = expr->argv[1];
	switch (mode[0]) {
	case '-':
		expr->mode_cmp = BFS_MODE_ALL;
		++mode;
		break;
	case '/':
		expr->mode_cmp = BFS_MODE_ANY;
		++mode;
		break;
	case '+':
		if (mode[1] >= '0' && mode[1] <= '9') {
			expr->mode_cmp = BFS_MODE_ANY;
			++mode;
			break;
		}
		fallthru;
	default:
		expr->mode_cmp = BFS_MODE_EQUAL;
		break;
	}

	if (parse_mode(parser, mode, expr) != 0) {
		return NULL;
	}

	return expr;
}

/**
 * Parse -print.
 */
static struct bfs_expr *parse_print(struct bfs_parser *parser, int arg1, int arg2) {
	struct bfs_expr *expr = parse_nullary_action(parser, eval_fprint);
	if (expr) {
		init_print_expr(parser, expr);
	}
	return expr;
}

/**
 * Parse -print0.
 */
static struct bfs_expr *parse_print0(struct bfs_parser *parser, int arg1, int arg2) {
	struct bfs_expr *expr = parse_nullary_action(parser, eval_fprint0);
	if (expr) {
		init_print_expr(parser, expr);
	}
	return expr;
}

/**
 * Parse -printf FORMAT.
 */
static struct bfs_expr *parse_printf(struct bfs_parser *parser, int arg1, int arg2) {
	struct bfs_expr *expr = parse_unary_action(parser, eval_fprintf);
	if (!expr) {
		return NULL;
	}

	init_print_expr(parser, expr);

	if (bfs_printf_parse(parser->ctx, expr, expr->argv[1]) != 0) {
		return NULL;
	}

	return expr;
}

/**
 * Parse -printx.
 */
static struct bfs_expr *parse_printx(struct bfs_parser *parser, int arg1, int arg2) {
	struct bfs_expr *expr = parse_nullary_action(parser, eval_fprintx);
	if (expr) {
		init_print_expr(parser, expr);
	}
	return expr;
}

/**
 * Parse -prune.
 */
static struct bfs_expr *parse_prune(struct bfs_parser *parser, int arg1, int arg2) {
	parser->prune_arg = parser->argv;
	return parse_nullary_action(parser, eval_prune);
}

/**
 * Parse -quit.
 */
static struct bfs_expr *parse_quit(struct bfs_parser *parser, int arg1, int arg2) {
	return parse_nullary_action(parser, eval_quit);
}

/**
 * Parse -i?regex.
 */
static struct bfs_expr *parse_regex(struct bfs_parser *parser, int flags, int arg2) {
	struct bfs_expr *expr = parse_unary_test(parser, eval_regex);
	if (!expr) {
		return NULL;
	}

	if (bfs_regcomp(&expr->regex, expr->argv[1], parser->regex_type, flags) != 0) {
		if (expr->regex) {
			char *str = bfs_regerror(expr->regex);
			if (str) {
				parse_expr_error(parser, expr, "%s.\n", str);
				free(str);
			} else {
				parse_perror(parser, "bfs_regerror()");
			}
		} else {
			parse_perror(parser, "bfs_regcomp()");
		}

		return NULL;
	}

	return expr;
}

/**
 * Parse -E.
 */
static struct bfs_expr *parse_regex_extended(struct bfs_parser *parser, int arg1, int arg2) {
	parser->regex_type = BFS_REGEX_POSIX_EXTENDED;
	return parse_nullary_flag(parser);
}

/**
 * Parse -regextype TYPE.
 */
static struct bfs_expr *parse_regextype(struct bfs_parser *parser, int arg1, int arg2) {
	struct bfs_ctx *ctx = parser->ctx;
	CFILE *cfile = ctx->cerr;

	struct bfs_expr *expr = parse_unary_option(parser);
	if (!expr) {
		cfprintf(cfile, "\n");
		goto list_types;
	}

	// See https://www.gnu.org/software/gnulib/manual/html_node/Predefined-Syntaxes.html
	const char *type = expr->argv[1];
	if (strcmp(type, "posix-basic") == 0
	    || strcmp(type, "ed") == 0
	    || strcmp(type, "sed") == 0) {
		parser->regex_type = BFS_REGEX_POSIX_BASIC;
	} else if (strcmp(type, "posix-extended") == 0) {
		parser->regex_type = BFS_REGEX_POSIX_EXTENDED;
#if BFS_USE_ONIGURUMA
	} else if (strcmp(type, "emacs") == 0) {
		parser->regex_type = BFS_REGEX_EMACS;
	} else if (strcmp(type, "grep") == 0) {
		parser->regex_type = BFS_REGEX_GREP;
#endif
	} else if (strcmp(type, "help") == 0) {
		parser->just_info = true;
		cfile = ctx->cout;
		goto list_types;
	} else {
		parse_expr_error(parser, expr, "Unsupported regex type.\n\n");
		goto list_types;
	}

	return expr;

list_types:
	cfprintf(cfile, "Supported types are:\n\n");
	cfprintf(cfile, "  ${bld}posix-basic${rs}:    POSIX basic regular expressions (BRE)\n");
	cfprintf(cfile, "  ${bld}posix-extended${rs}: POSIX extended regular expressions (ERE)\n");
	cfprintf(cfile, "  ${bld}ed${rs}:             Like ${grn}ed${rs} (same as ${bld}posix-basic${rs})\n");
#if BFS_USE_ONIGURUMA
	cfprintf(cfile, "  ${bld}emacs${rs}:          Like ${grn}emacs${rs}\n");
	cfprintf(cfile, "  ${bld}grep${rs}:           Like ${grn}grep${rs}\n");
#endif
	cfprintf(cfile, "  ${bld}sed${rs}:            Like ${grn}sed${rs} (same as ${bld}posix-basic${rs})\n");
	return NULL;
}

/**
 * Parse -s.
 */
static struct bfs_expr *parse_s(struct bfs_parser *parser, int arg1, int arg2) {
	parser->ctx->flags |= BFTW_SORT;
	return parse_nullary_flag(parser);
}

/**
 * Parse -samefile FILE.
 */
static struct bfs_expr *parse_samefile(struct bfs_parser *parser, int arg1, int arg2) {
	struct bfs_expr *expr = parse_unary_test(parser, eval_samefile);
	if (!expr) {
		return NULL;
	}

	struct bfs_stat sb;
	if (stat_arg(parser, &expr->argv[1], &sb) != 0) {
		return NULL;
	}

	expr->dev = sb.dev;
	expr->ino = sb.ino;
	return expr;
}

/**
 * Parse -S STRATEGY.
 */
static struct bfs_expr *parse_search_strategy(struct bfs_parser *parser, int arg1, int arg2) {
	struct bfs_ctx *ctx = parser->ctx;
	CFILE *cfile = ctx->cerr;

	struct bfs_expr *expr = parse_unary_flag(parser);
	if (!expr) {
		cfprintf(cfile, "\n");
		goto list_strategies;
	}

	const char *arg = expr->argv[1];
	if (strcmp(arg, "bfs") == 0) {
		ctx->strategy = BFTW_BFS;
	} else if (strcmp(arg, "dfs") == 0) {
		ctx->strategy = BFTW_DFS;
	} else if (strcmp(arg, "ids") == 0) {
		ctx->strategy = BFTW_IDS;
	} else if (strcmp(arg, "eds") == 0) {
		ctx->strategy = BFTW_EDS;
	} else if (strcmp(arg, "help") == 0) {
		parser->just_info = true;
		cfile = ctx->cout;
		goto list_strategies;
	} else {
		parse_expr_error(parser, expr, "Unrecognized search strategy.\n\n");
		goto list_strategies;
	}

	return expr;

list_strategies:
	cfprintf(cfile, "Supported search strategies:\n\n");
	cfprintf(cfile, "  ${bld}bfs${rs}: breadth-first search\n");
	cfprintf(cfile, "  ${bld}dfs${rs}: depth-first search\n");
	cfprintf(cfile, "  ${bld}ids${rs}: iterative deepening search\n");
	cfprintf(cfile, "  ${bld}eds${rs}: exponential deepening search\n");
	return NULL;
}

/**
 * Parse -[aBcm]?since.
 */
static struct bfs_expr *parse_since(struct bfs_parser *parser, int field, int arg2) {
	struct bfs_expr *expr = parse_unary_test(parser, eval_newer);
	if (!expr) {
		return NULL;
	}

	if (parse_reftime(parser, expr) != 0) {
		return NULL;
	}

	expr->stat_field = field;
	return expr;
}

/**
 * Parse -size N[cwbkMGTP]?.
 */
static struct bfs_expr *parse_size(struct bfs_parser *parser, int arg1, int arg2) {
	struct bfs_expr *expr = parse_unary_test(parser, eval_size);
	if (!expr) {
		return NULL;
	}

	const char *unit = parse_icmp(parser, expr, IF_PARTIAL_OK);
	if (!unit) {
		return NULL;
	}

	if (strlen(unit) > 1) {
		goto bad_unit;
	}

	switch (*unit) {
	case '\0':
	case 'b':
		expr->size_unit = BFS_BLOCKS;
		break;
	case 'c':
		expr->size_unit = BFS_BYTES;
		break;
	case 'w':
		expr->size_unit = BFS_WORDS;
		break;
	case 'k':
		expr->size_unit = BFS_KB;
		break;
	case 'M':
		expr->size_unit = BFS_MB;
		break;
	case 'G':
		expr->size_unit = BFS_GB;
		break;
	case 'T':
		expr->size_unit = BFS_TB;
		break;
	case 'P':
		expr->size_unit = BFS_PB;
		break;

	default:
	bad_unit:
		parse_expr_error(parser, expr, "Expected a size unit (one of ${bld}cwbkMGTP${rs}); found ${err}%pq${rs}.\n", unit);
		return NULL;
	}

	return expr;
}

/**
 * Parse -sparse.
 */
static struct bfs_expr *parse_sparse(struct bfs_parser *parser, int arg1, int arg2) {
	return parse_nullary_test(parser, eval_sparse);
}

/**
 * Parse -status.
 */
static struct bfs_expr *parse_status(struct bfs_parser *parser, int arg1, int arg2) {
	parser->ctx->status = true;
	return parse_nullary_option(parser);
}

/**
 * Parse -x?type [bcdpflsD].
 */
static struct bfs_expr *parse_type(struct bfs_parser *parser, int x, int arg2) {
	struct bfs_ctx *ctx = parser->ctx;

	bfs_eval_fn *eval = x ? eval_xtype : eval_type;
	struct bfs_expr *expr = parse_unary_test(parser, eval);
	if (!expr) {
		return NULL;
	}

	expr->num = 0;

	const char *c = expr->argv[1];
	while (true) {
		switch (*c) {
		case 'b':
			expr->num |= 1 << BFS_BLK;
			break;
		case 'c':
			expr->num |= 1 << BFS_CHR;
			break;
		case 'd':
			expr->num |= 1 << BFS_DIR;
			break;
		case 'D':
			expr->num |= 1 << BFS_DOOR;
			break;
		case 'p':
			expr->num |= 1 << BFS_FIFO;
			break;
		case 'f':
			expr->num |= 1 << BFS_REG;
			break;
		case 'l':
			expr->num |= 1 << BFS_LNK;
			break;
		case 's':
			expr->num |= 1 << BFS_SOCK;
			break;
		case 'w':
			expr->num |= 1 << BFS_WHT;
			ctx->flags |= BFTW_WHITEOUTS;
			break;

		case '\0':
			parse_expr_error(parser, expr, "Expected a type flag.\n");
			return NULL;

		default:
			parse_expr_error(parser, expr, "Unknown type flag ${err}%c${rs}; expected one of [${bld}bcdpflsD${rs}].\n", *c);
			return NULL;
		}

		++c;
		if (*c == '\0') {
			break;
		} else if (*c == ',') {
			++c;
			continue;
		} else {
			parse_expr_error(parser, expr, "Types must be comma-separated.\n");
			return NULL;
		}
	}

	return expr;
}

/**
 * Parse -(no)?warn.
 */
static struct bfs_expr *parse_warn(struct bfs_parser *parser, int warn, int arg2) {
	parser->ctx->warn = warn;
	return parse_nullary_option(parser);
}

/**
 * Parse -xattr.
 */
static struct bfs_expr *parse_xattr(struct bfs_parser *parser, int arg1, int arg2) {
#if BFS_CAN_CHECK_XATTRS
	return parse_nullary_test(parser, eval_xattr);
#else
	parse_error(parser, "Missing platform support.\n");
	return NULL;
#endif
}

/**
 * Parse -xattrname.
 */
static struct bfs_expr *parse_xattrname(struct bfs_parser *parser, int arg1, int arg2) {
#if BFS_CAN_CHECK_XATTRS
	return parse_unary_test(parser, eval_xattrname);
#else
	parse_error(parser, "Missing platform support.\n");
	return NULL;
#endif
}

/**
 * Parse -xdev.
 */
static struct bfs_expr *parse_xdev(struct bfs_parser *parser, int arg1, int arg2) {
	parser->ctx->flags |= BFTW_PRUNE_MOUNTS;
	parser->xdev_arg = parser->argv;
	return parse_nullary_option(parser);
}

/**
 * Launch a pager for the help output.
 */
static CFILE *launch_pager(pid_t *pid, CFILE *cout) {
	char *pager = getenv("PAGER");

	char *exe;
	if (pager && pager[0]) {
		exe = bfs_spawn_resolve(pager);
	} else {
		exe = bfs_spawn_resolve("less");
		if (!exe) {
			exe = bfs_spawn_resolve("more");
		}
	}
	if (!exe) {
		goto fail;
	}

	int pipefd[2];
	if (pipe(pipefd) != 0) {
		goto fail_exe;
	}

	FILE *file = fdopen(pipefd[1], "w");
	if (!file) {
		goto fail_pipe;
	}
	pipefd[1] = -1;

	CFILE *ret = cfwrap(file, NULL, true);
	if (!ret) {
		goto fail_file;
	}
	file = NULL;

	struct bfs_spawn ctx;
	if (bfs_spawn_init(&ctx) != 0) {
		goto fail_ret;
	}

	if (bfs_spawn_addclose(&ctx, fileno(ret->file)) != 0) {
		goto fail_ctx;
	}
	if (bfs_spawn_adddup2(&ctx, pipefd[0], STDIN_FILENO) != 0) {
		goto fail_ctx;
	}
	if (bfs_spawn_addclose(&ctx, pipefd[0]) != 0) {
		goto fail_ctx;
	}

	char *argv[] = {
		exe,
		NULL,
		NULL,
	};

	const char *cmd = exe + xbaseoff(exe);
	if (strcmp(cmd, "less") == 0) {
		// We know less supports colors, other pagers may not
		ret->colors = cout->colors;
		argv[1] = "-FKRX";
	}

	*pid = bfs_spawn(exe, &ctx, argv, NULL);
	if (*pid < 0) {
		goto fail_ctx;
	}

	xclose(pipefd[0]);
	bfs_spawn_destroy(&ctx);
	free(exe);
	return ret;

fail_ctx:
	bfs_spawn_destroy(&ctx);
fail_ret:
	cfclose(ret);
fail_file:
	if (file) {
		fclose(file);
	}
fail_pipe:
	if (pipefd[1] >= 0) {
		xclose(pipefd[1]);
	}
	if (pipefd[0] >= 0) {
		xclose(pipefd[0]);
	}
fail_exe:
	free(exe);
fail:
	return cout;
}

/**
 * "Parse" -help.
 */
static struct bfs_expr *parse_help(struct bfs_parser *parser, int arg1, int arg2) {
	CFILE *cout = parser->ctx->cout;

	pid_t pager = -1;
	if (parser->stdout_tty) {
		cout = launch_pager(&pager, cout);
	}

	cfprintf(cout, "Usage: ${ex}%s${rs} [${cyn}flags${rs}...] [${mag}paths${rs}...] [${blu}expression${rs}...]\n\n",
		parser->command);

	cfprintf(cout, "${ex}%s${rs} is compatible with ${ex}find${rs}, with some extensions. "
	               "${cyn}Flags${rs} (${cyn}-H${rs}/${cyn}-L${rs}/${cyn}-P${rs} etc.), ${mag}paths${rs},\n"
	               "and ${blu}expressions${rs} may be freely mixed in any order.\n\n",
		BFS_COMMAND);

	cfprintf(cout, "${bld}Flags:${rs}\n\n");

	cfprintf(cout, "  ${cyn}-H${rs}\n");
	cfprintf(cout, "      Follow symbolic links on the command line, but not while searching\n");
	cfprintf(cout, "  ${cyn}-L${rs}\n");
	cfprintf(cout, "      Follow all symbolic links\n");
	cfprintf(cout, "  ${cyn}-P${rs}\n");
	cfprintf(cout, "      Never follow symbolic links (the default)\n");

	cfprintf(cout, "  ${cyn}-E${rs}\n");
	cfprintf(cout, "      Use extended regular expressions (same as ${blu}-regextype${rs} ${bld}posix-extended${rs})\n");
	cfprintf(cout, "  ${cyn}-X${rs}\n");
	cfprintf(cout, "      Filter out files with non-${ex}xargs${rs}-safe names\n");
	cfprintf(cout, "  ${cyn}-d${rs}\n");
	cfprintf(cout, "      Search in post-order (same as ${blu}-depth${rs})\n");
	cfprintf(cout, "  ${cyn}-s${rs}\n");
	cfprintf(cout, "      Visit directory entries in sorted order\n");
	cfprintf(cout, "  ${cyn}-x${rs}\n");
	cfprintf(cout, "      Don't descend into other mount points (same as ${blu}-xdev${rs})\n");

	cfprintf(cout, "  ${cyn}-f${rs} ${mag}PATH${rs}\n");
	cfprintf(cout, "      Treat ${mag}PATH${rs} as a path to search (useful if begins with a dash)\n");
	cfprintf(cout, "  ${cyn}-D${rs} ${bld}FLAG${rs}\n");
	cfprintf(cout, "      Turn on a debugging flag (see ${cyn}-D${rs} ${bld}help${rs})\n");
	cfprintf(cout, "  ${cyn}-O${bld}N${rs}\n");
	cfprintf(cout, "      Enable optimization level ${bld}N${rs} (default: ${bld}3${rs})\n");
	cfprintf(cout, "  ${cyn}-S${rs} ${bld}bfs${rs}|${bld}dfs${rs}|${bld}ids${rs}|${bld}eds${rs}\n");
	cfprintf(cout, "      Use ${bld}b${rs}readth-${bld}f${rs}irst/${bld}d${rs}epth-${bld}f${rs}irst/${bld}i${rs}terative/${bld}e${rs}xponential ${bld}d${rs}eepening ${bld}s${rs}earch\n");
	cfprintf(cout, "      (default: ${cyn}-S${rs} ${bld}bfs${rs})\n");
	cfprintf(cout, "  ${cyn}-j${bld}N${rs}\n");
	cfprintf(cout, "      Search with ${bld}N${rs} threads in parallel (default: number of CPUs, up to ${bld}8${rs})\n\n");

	cfprintf(cout, "${bld}Operators:${rs}\n\n");

	cfprintf(cout, "  ${red}(${rs} ${blu}expression${rs} ${red})${rs}\n\n");

	cfprintf(cout, "  ${red}!${rs} ${blu}expression${rs}\n");
	cfprintf(cout, "  ${red}-not${rs} ${blu}expression${rs}\n\n");

	cfprintf(cout, "  ${blu}expression${rs} ${blu}expression${rs}\n");
	cfprintf(cout, "  ${blu}expression${rs} ${red}-a${rs} ${blu}expression${rs}\n");
	cfprintf(cout, "  ${blu}expression${rs} ${red}-and${rs} ${blu}expression${rs}\n\n");

	cfprintf(cout, "  ${blu}expression${rs} ${red}-o${rs} ${blu}expression${rs}\n");
	cfprintf(cout, "  ${blu}expression${rs} ${red}-or${rs} ${blu}expression${rs}\n\n");

	cfprintf(cout, "  ${blu}expression${rs} ${red},${rs} ${blu}expression${rs}\n\n");

	cfprintf(cout, "${bld}Special forms:${rs}\n\n");

	cfprintf(cout, "  ${red}-exclude${rs} ${blu}expression${rs}\n");
	cfprintf(cout, "      Exclude all paths matching the ${blu}expression${rs} from the search.\n\n");

	cfprintf(cout, "${bld}Options:${rs}\n\n");

	cfprintf(cout, "  ${blu}-color${rs}\n");
	cfprintf(cout, "  ${blu}-nocolor${rs}\n");
	cfprintf(cout, "      Turn colors on or off (default: ${blu}-color${rs} if outputting to a terminal,\n");
	cfprintf(cout, "      ${blu}-nocolor${rs} otherwise)\n");
	cfprintf(cout, "  ${blu}-daystart${rs}\n");
	cfprintf(cout, "      Measure times relative to the start of today\n");
	cfprintf(cout, "  ${blu}-depth${rs}\n");
	cfprintf(cout, "      Search in post-order (descendents first)\n");
	cfprintf(cout, "  ${blu}-files0-from${rs} ${bld}FILE${rs}\n");
	cfprintf(cout, "      Search the NUL ('\\0')-separated paths from ${bld}FILE${rs} (${bld}-${rs} for standard input).\n");
	cfprintf(cout, "  ${blu}-follow${rs}\n");
	cfprintf(cout, "      Follow all symbolic links (same as ${cyn}-L${rs})\n");
	cfprintf(cout, "  ${blu}-ignore_readdir_race${rs}\n");
	cfprintf(cout, "  ${blu}-noignore_readdir_race${rs}\n");
	cfprintf(cout, "      Whether to report an error if ${ex}%s${rs} detects that the file tree is modified\n",
		BFS_COMMAND);
	cfprintf(cout, "      during the search (default: ${blu}-noignore_readdir_race${rs})\n");
	cfprintf(cout, "  ${blu}-maxdepth${rs} ${bld}N${rs}\n");
	cfprintf(cout, "  ${blu}-mindepth${rs} ${bld}N${rs}\n");
	cfprintf(cout, "      Ignore files deeper/shallower than ${bld}N${rs}\n");
	cfprintf(cout, "  ${blu}-mount${rs}\n");
	cfprintf(cout, "      Don't descend into other mount points (same as ${blu}-xdev${rs} for now, but will\n");
	cfprintf(cout, "      skip mount points entirely in the future)\n");
	cfprintf(cout, "  ${blu}-nohidden${rs}\n");
	cfprintf(cout, "      Exclude hidden files\n");
	cfprintf(cout, "  ${blu}-noleaf${rs}\n");
	cfprintf(cout, "      Ignored; for compatibility with GNU find\n");
	cfprintf(cout, "  ${blu}-regextype${rs} ${bld}TYPE${rs}\n");
	cfprintf(cout, "      Use ${bld}TYPE${rs}-flavored regexes (default: ${bld}posix-basic${rs}; see ${blu}-regextype${rs} ${bld}help${rs})\n");
	cfprintf(cout, "  ${blu}-status${rs}\n");
	cfprintf(cout, "      Display a status bar while searching\n");
	cfprintf(cout, "  ${blu}-unique${rs}\n");
	cfprintf(cout, "      Skip any files that have already been seen\n");
	cfprintf(cout, "  ${blu}-warn${rs}\n");
	cfprintf(cout, "  ${blu}-nowarn${rs}\n");
	cfprintf(cout, "      Turn on or off warnings about the command line\n");
	cfprintf(cout, "  ${blu}-xdev${rs}\n");
	cfprintf(cout, "      Don't descend into other mount points\n\n");

	cfprintf(cout, "${bld}Tests:${rs}\n\n");

#if BFS_CAN_CHECK_ACL
	cfprintf(cout, "  ${blu}-acl${rs}\n");
	cfprintf(cout, "      Find files with a non-trivial Access Control List\n");
#endif
	cfprintf(cout, "  ${blu}-${rs}[${blu}aBcm${rs}]${blu}min${rs} ${bld}[-+]N${rs}\n");
	cfprintf(cout, "      Find files ${blu}a${rs}ccessed/${blu}B${rs}irthed/${blu}c${rs}hanged/${blu}m${rs}odified ${bld}N${rs} minutes ago\n");
	cfprintf(cout, "  ${blu}-${rs}[${blu}aBcm${rs}]${blu}newer${rs} ${bld}FILE${rs}\n");
	cfprintf(cout, "      Find files ${blu}a${rs}ccessed/${blu}B${rs}irthed/${blu}c${rs}hanged/${blu}m${rs}odified more recently than ${bld}FILE${rs} was\n"
	               "      modified\n");
	cfprintf(cout, "  ${blu}-${rs}[${blu}aBcm${rs}]${blu}since${rs} ${bld}TIME${rs}\n");
	cfprintf(cout, "      Find files ${blu}a${rs}ccessed/${blu}B${rs}irthed/${blu}c${rs}hanged/${blu}m${rs}odified more recently than ${bld}TIME${rs}\n");
	cfprintf(cout, "  ${blu}-${rs}[${blu}aBcm${rs}]${blu}time${rs} ${bld}[-+]N${rs}\n");
	cfprintf(cout, "      Find files ${blu}a${rs}ccessed/${blu}B${rs}irthed/${blu}c${rs}hanged/${blu}m${rs}odified ${bld}N${rs} days ago\n");
#if BFS_CAN_CHECK_CAPABILITIES
	cfprintf(cout, "  ${blu}-capable${rs}\n");
	cfprintf(cout, "      Find files with POSIX.1e capabilities set\n");
#endif
	cfprintf(cout, "  ${blu}-depth${rs} ${bld}[-+]N${rs}\n");
	cfprintf(cout, "      Find files with depth ${bld}N${rs}\n");
	cfprintf(cout, "  ${blu}-empty${rs}\n");
	cfprintf(cout, "      Find empty files/directories\n");
	cfprintf(cout, "  ${blu}-executable${rs}\n");
	cfprintf(cout, "  ${blu}-readable${rs}\n");
	cfprintf(cout, "  ${blu}-writable${rs}\n");
	cfprintf(cout, "      Find files the current user can execute/read/write\n");
	cfprintf(cout, "  ${blu}-false${rs}\n");
	cfprintf(cout, "  ${blu}-true${rs}\n");
	cfprintf(cout, "      Always false/true\n");
	cfprintf(cout, "  ${blu}-fstype${rs} ${bld}TYPE${rs}\n");
	cfprintf(cout, "      Find files on file systems with the given ${bld}TYPE${rs}\n");
	cfprintf(cout, "  ${blu}-gid${rs} ${bld}[-+]N${rs}\n");
	cfprintf(cout, "  ${blu}-uid${rs} ${bld}[-+]N${rs}\n");
	cfprintf(cout, "      Find files owned by group/user ID ${bld}N${rs}\n");
	cfprintf(cout, "  ${blu}-group${rs} ${bld}NAME${rs}\n");
	cfprintf(cout, "  ${blu}-user${rs}  ${bld}NAME${rs}\n");
	cfprintf(cout, "      Find files owned by the group/user ${bld}NAME${rs}\n");
	cfprintf(cout, "  ${blu}-hidden${rs}\n");
	cfprintf(cout, "      Find hidden files\n");
#ifdef FNM_CASEFOLD
	cfprintf(cout, "  ${blu}-ilname${rs} ${bld}GLOB${rs}\n");
	cfprintf(cout, "  ${blu}-iname${rs}  ${bld}GLOB${rs}\n");
	cfprintf(cout, "  ${blu}-ipath${rs}  ${bld}GLOB${rs}\n");
	cfprintf(cout, "  ${blu}-iregex${rs} ${bld}REGEX${rs}\n");
	cfprintf(cout, "  ${blu}-iwholename${rs} ${bld}GLOB${rs}\n");
	cfprintf(cout, "      Case-insensitive versions of ${blu}-lname${rs}/${blu}-name${rs}/${blu}-path${rs}"
	               "/${blu}-regex${rs}/${blu}-wholename${rs}\n");
#endif
	cfprintf(cout, "  ${blu}-inum${rs} ${bld}[-+]N${rs}\n");
	cfprintf(cout, "      Find files with inode number ${bld}N${rs}\n");
	cfprintf(cout, "  ${blu}-links${rs} ${bld}[-+]N${rs}\n");
	cfprintf(cout, "      Find files with ${bld}N${rs} hard links\n");
	cfprintf(cout, "  ${blu}-lname${rs} ${bld}GLOB${rs}\n");
	cfprintf(cout, "      Find symbolic links whose target matches the ${bld}GLOB${rs}\n");
	cfprintf(cout, "  ${blu}-name${rs} ${bld}GLOB${rs}\n");
	cfprintf(cout, "      Find files whose name matches the ${bld}GLOB${rs}\n");
	cfprintf(cout, "  ${blu}-newer${rs} ${bld}FILE${rs}\n");
	cfprintf(cout, "      Find files newer than ${bld}FILE${rs}\n");
	cfprintf(cout, "  ${blu}-newer${bld}XY${rs} ${bld}REFERENCE${rs}\n");
	cfprintf(cout, "      Find files whose ${bld}X${rs} time is newer than the ${bld}Y${rs} time of"
	               " ${bld}REFERENCE${rs}.  ${bld}X${rs} and ${bld}Y${rs}\n");
	cfprintf(cout, "      can be any of [${bld}aBcm${rs}].  ${bld}Y${rs} may also be ${bld}t${rs} to parse ${bld}REFERENCE${rs} an explicit\n");
	cfprintf(cout, "      timestamp.\n");
	cfprintf(cout, "  ${blu}-nogroup${rs}\n");
	cfprintf(cout, "  ${blu}-nouser${rs}\n");
	cfprintf(cout, "      Find files owned by nonexistent groups/users\n");
	cfprintf(cout, "  ${blu}-path${rs} ${bld}GLOB${rs}\n");
	cfprintf(cout, "  ${blu}-wholename${rs} ${bld}GLOB${rs}\n");
	cfprintf(cout, "      Find files whose entire path matches the ${bld}GLOB${rs}\n");
	cfprintf(cout, "  ${blu}-perm${rs} ${bld}[-]MODE${rs}\n");
	cfprintf(cout, "      Find files with a matching mode\n");
	cfprintf(cout, "  ${blu}-regex${rs} ${bld}REGEX${rs}\n");
	cfprintf(cout, "      Find files whose entire path matches the regular expression ${bld}REGEX${rs}\n");
	cfprintf(cout, "  ${blu}-samefile${rs} ${bld}FILE${rs}\n");
	cfprintf(cout, "      Find hard links to ${bld}FILE${rs}\n");
	cfprintf(cout, "  ${blu}-since${rs} ${bld}TIME${rs}\n");
	cfprintf(cout, "      Find files modified since ${bld}TIME${rs}\n");
	cfprintf(cout, "  ${blu}-size${rs} ${bld}[-+]N[cwbkMGTP]${rs}\n");
	cfprintf(cout, "      Find files with the given size, in 1-byte ${bld}c${rs}haracters, 2-byte ${bld}w${rs}ords,\n");
	cfprintf(cout, "      512-byte ${bld}b${rs}locks (default), or ${bld}k${rs}iB/${bld}M${rs}iB/${bld}G${rs}iB/${bld}T${rs}iB/${bld}P${rs}iB\n");
	cfprintf(cout, "  ${blu}-sparse${rs}\n");
	cfprintf(cout, "      Find files that occupy fewer disk blocks than expected\n");
	cfprintf(cout, "  ${blu}-type${rs} ${bld}[bcdlpfswD]${rs}\n");
	cfprintf(cout, "      Find files of the given type\n");
	cfprintf(cout, "  ${blu}-used${rs} ${bld}[-+]N${rs}\n");
	cfprintf(cout, "      Find files last accessed ${bld}N${rs} days after they were changed\n");
#if BFS_CAN_CHECK_XATTRS
	cfprintf(cout, "  ${blu}-xattr${rs}\n");
	cfprintf(cout, "      Find files with extended attributes\n");
	cfprintf(cout, "  ${blu}-xattrname${rs} ${bld}NAME${rs}\n");
	cfprintf(cout, "      Find files with the extended attribute ${bld}NAME${rs}\n");
#endif
	cfprintf(cout, "  ${blu}-xtype${rs} ${bld}[bcdlpfswD]${rs}\n");
	cfprintf(cout, "      Find files of the given type, following links when ${blu}-type${rs} would not, and\n");
	cfprintf(cout, "      vice versa\n\n");

	cfprintf(cout, "${bld}Actions:${rs}\n\n");

	cfprintf(cout, "  ${blu}-delete${rs}\n");
	cfprintf(cout, "  ${blu}-rm${rs}\n");
	cfprintf(cout, "      Delete any found files (implies ${blu}-depth${rs})\n");
	cfprintf(cout, "  ${blu}-exec${rs} ${bld}command ... {} ;${rs}\n");
	cfprintf(cout, "      Execute a command\n");
	cfprintf(cout, "  ${blu}-exec${rs} ${bld}command ... {} +${rs}\n");
	cfprintf(cout, "      Execute a command with multiple files at once\n");
	cfprintf(cout, "  ${blu}-ok${rs} ${bld}command ... {} ;${rs}\n");
	cfprintf(cout, "      Prompt the user whether to execute a command\n");
	cfprintf(cout, "  ${blu}-execdir${rs} ${bld}command ... {} ;${rs}\n");
	cfprintf(cout, "  ${blu}-execdir${rs} ${bld}command ... {} +${rs}\n");
	cfprintf(cout, "  ${blu}-okdir${rs} ${bld}command ... {} ;${rs}\n");
	cfprintf(cout, "      Like ${blu}-exec${rs}/${blu}-ok${rs}, but run the command in the same directory as the found\n");
	cfprintf(cout, "      file(s)\n");
	cfprintf(cout, "  ${blu}-exit${rs} [${bld}STATUS${rs}]\n");
	cfprintf(cout, "      Exit immediately with the given status (%d if unspecified)\n", EXIT_SUCCESS);
	cfprintf(cout, "  ${blu}-fls${rs} ${bld}FILE${rs}\n");
	cfprintf(cout, "  ${blu}-fprint${rs} ${bld}FILE${rs}\n");
	cfprintf(cout, "  ${blu}-fprint0${rs} ${bld}FILE${rs}\n");
	cfprintf(cout, "  ${blu}-fprintf${rs} ${bld}FILE${rs} ${bld}FORMAT${rs}\n");
	cfprintf(cout, "      Like ${blu}-ls${rs}/${blu}-print${rs}/${blu}-print0${rs}/${blu}-printf${rs}, but write to ${bld}FILE${rs} instead of standard\n"
	               "      output\n");
	cfprintf(cout, "  ${blu}-ls${rs}\n");
	cfprintf(cout, "      List files like ${ex}ls${rs} ${bld}-dils${rs}\n");
	cfprintf(cout, "  ${blu}-print${rs}\n");
	cfprintf(cout, "      Print the path to the found file\n");
	cfprintf(cout, "  ${blu}-print0${rs}\n");
	cfprintf(cout, "      Like ${blu}-print${rs}, but use the null character ('\\0') as a separator rather than\n");
	cfprintf(cout, "      newlines\n");
	cfprintf(cout, "  ${blu}-printf${rs} ${bld}FORMAT${rs}\n");
	cfprintf(cout, "      Print according to a format string (see ${ex}man${rs} ${bld}find${rs}).  The additional format\n");
	cfprintf(cout, "      directives %%w and %%W${bld}k${rs} for printing file birth times are supported.\n");
	cfprintf(cout, "  ${blu}-printx${rs}\n");
	cfprintf(cout, "      Like ${blu}-print${rs}, but escape whitespace and quotation characters, to make the\n");
	cfprintf(cout, "      output safe for ${ex}xargs${rs}.  Consider using ${blu}-print0${rs} and ${ex}xargs${rs} ${bld}-0${rs} instead.\n");
	cfprintf(cout, "  ${blu}-prune${rs}\n");
	cfprintf(cout, "      Don't descend into this directory\n");
	cfprintf(cout, "  ${blu}-quit${rs}\n");
	cfprintf(cout, "      Quit immediately\n");
	cfprintf(cout, "  ${blu}-version${rs}\n");
	cfprintf(cout, "      Print version information\n");
	cfprintf(cout, "  ${blu}-help${rs}\n");
	cfprintf(cout, "      Print this help message\n\n");

	cfprintf(cout, "%s\n", BFS_HOMEPAGE);

	if (pager > 0) {
		cfclose(cout);
		xwaitpid(pager, NULL, 0);
	}

	parser->just_info = true;
	return NULL;
}

/**
 * "Parse" -version.
 */
static struct bfs_expr *parse_version(struct bfs_parser *parser, int arg1, int arg2) {
	cfprintf(parser->ctx->cout, "${ex}%s${rs} ${bld}%s${rs}\n\n", BFS_COMMAND, BFS_VERSION);

	printf("%s\n", BFS_HOMEPAGE);

	parser->just_info = true;
	return NULL;
}

typedef struct bfs_expr *parse_fn(struct bfs_parser *parser, int arg1, int arg2);

/**
 * An entry in the parse table for primary expressions.
 */
struct table_entry {
	char *arg;
	enum token_type type;
	parse_fn *parse;
	int arg1;
	int arg2;
	bool prefix;
};

/**
 * The parse table for primary expressions.
 */
static const struct table_entry parse_table[] = {
	{"--", T_FLAG},
	{"--help", T_ACTION, parse_help},
	{"--version", T_ACTION, parse_version},
	{"-Bmin", T_TEST, parse_min, BFS_STAT_BTIME},
	{"-Bnewer", T_TEST, parse_newer, BFS_STAT_BTIME},
	{"-Bsince", T_TEST, parse_since, BFS_STAT_BTIME},
	{"-Btime", T_TEST, parse_time, BFS_STAT_BTIME},
	{"-D", T_FLAG, parse_debug},
	{"-E", T_FLAG, parse_regex_extended},
	{"-H", T_FLAG, parse_follow, BFTW_FOLLOW_ROOTS, false},
	{"-L", T_FLAG, parse_follow, BFTW_FOLLOW_ALL, false},
	{"-O", T_FLAG, parse_optlevel, 0, 0, true},
	{"-P", T_FLAG, parse_follow, 0, false},
	{"-S", T_FLAG, parse_search_strategy},
	{"-X", T_FLAG, parse_xargs_safe},
	{"-a", T_OPERATOR},
	{"-acl", T_TEST, parse_acl},
	{"-amin", T_TEST, parse_min, BFS_STAT_ATIME},
	{"-and", T_OPERATOR},
	{"-anewer", T_TEST, parse_newer, BFS_STAT_ATIME},
	{"-asince", T_TEST, parse_since, BFS_STAT_ATIME},
	{"-atime", T_TEST, parse_time, BFS_STAT_ATIME},
	{"-capable", T_TEST, parse_capable},
	{"-cmin", T_TEST, parse_min, BFS_STAT_CTIME},
	{"-cnewer", T_TEST, parse_newer, BFS_STAT_CTIME},
	{"-color", T_OPTION, parse_color, true},
	{"-csince", T_TEST, parse_since, BFS_STAT_CTIME},
	{"-ctime", T_TEST, parse_time, BFS_STAT_CTIME},
	{"-d", T_FLAG, parse_depth},
	{"-daystart", T_OPTION, parse_daystart},
	{"-delete", T_ACTION, parse_delete},
	{"-depth", T_OPTION, parse_depth_n},
	{"-empty", T_TEST, parse_empty},
	{"-exclude", T_OPERATOR},
	{"-exec", T_ACTION, parse_exec, 0},
	{"-execdir", T_ACTION, parse_exec, BFS_EXEC_CHDIR},
	{"-executable", T_TEST, parse_access, X_OK},
	{"-exit", T_ACTION, parse_exit},
	{"-f", T_FLAG, parse_f},
	{"-false", T_TEST, parse_const, false},
	{"-files0-from", T_OPTION, parse_files0_from},
	{"-flags", T_TEST, parse_flags},
	{"-fls", T_ACTION, parse_fls},
	{"-follow", T_OPTION, parse_follow, BFTW_FOLLOW_ALL, true},
	{"-fprint", T_ACTION, parse_fprint},
	{"-fprint0", T_ACTION, parse_fprint0},
	{"-fprintf", T_ACTION, parse_fprintf},
	{"-fstype", T_TEST, parse_fstype},
	{"-gid", T_TEST, parse_group},
	{"-group", T_TEST, parse_group},
	{"-help", T_ACTION, parse_help},
	{"-hidden", T_TEST, parse_hidden},
	{"-ignore_readdir_race", T_OPTION, parse_ignore_races, true},
	{"-ilname", T_TEST, parse_lname, true},
	{"-iname", T_TEST, parse_name, true},
	{"-inum", T_TEST, parse_inum},
	{"-ipath", T_TEST, parse_path, true},
	{"-iregex", T_TEST, parse_regex, BFS_REGEX_ICASE},
	{"-iwholename", T_TEST, parse_path, true},
	{"-j", T_FLAG, parse_jobs, 0, 0, true},
	{"-links", T_TEST, parse_links},
	{"-lname", T_TEST, parse_lname, false},
	{"-ls", T_ACTION, parse_ls},
	{"-maxdepth", T_OPTION, parse_depth_limit, false},
	{"-mindepth", T_OPTION, parse_depth_limit, true},
	{"-mmin", T_TEST, parse_min, BFS_STAT_MTIME},
	{"-mnewer", T_TEST, parse_newer, BFS_STAT_MTIME},
	{"-mount", T_OPTION, parse_mount},
	{"-msince", T_TEST, parse_since, BFS_STAT_MTIME},
	{"-mtime", T_TEST, parse_time, BFS_STAT_MTIME},
	{"-name", T_TEST, parse_name, false},
	{"-newer", T_TEST, parse_newer, BFS_STAT_MTIME},
	{"-newer", T_TEST, parse_newerxy, 0, 0, true},
	{"-nocolor", T_OPTION, parse_color, false},
	{"-nogroup", T_TEST, parse_nogroup},
	{"-nohidden", T_TEST, parse_nohidden},
	{"-noignore_readdir_race", T_OPTION, parse_ignore_races, false},
	{"-noleaf", T_OPTION, parse_noleaf},
	{"-not", T_OPERATOR},
	{"-nouser", T_TEST, parse_nouser},
	{"-nowarn", T_OPTION, parse_warn, false},
	{"-o", T_OPERATOR},
	{"-ok", T_ACTION, parse_exec, BFS_EXEC_CONFIRM},
	{"-okdir", T_ACTION, parse_exec, BFS_EXEC_CONFIRM | BFS_EXEC_CHDIR},
	{"-or", T_OPERATOR},
	{"-path", T_TEST, parse_path, false},
	{"-perm", T_TEST, parse_perm},
	{"-print", T_ACTION, parse_print},
	{"-print0", T_ACTION, parse_print0},
	{"-printf", T_ACTION, parse_printf},
	{"-printx", T_ACTION, parse_printx},
	{"-prune", T_ACTION, parse_prune},
	{"-quit", T_ACTION, parse_quit},
	{"-readable", T_TEST, parse_access, R_OK},
	{"-regex", T_TEST, parse_regex, 0},
	{"-regextype", T_OPTION, parse_regextype},
	{"-rm", T_ACTION, parse_delete},
	{"-s", T_FLAG, parse_s},
	{"-samefile", T_TEST, parse_samefile},
	{"-since", T_TEST, parse_since, BFS_STAT_MTIME},
	{"-size", T_TEST, parse_size},
	{"-sparse", T_TEST, parse_sparse},
	{"-status", T_OPTION, parse_status},
	{"-true", T_TEST, parse_const, true},
	{"-type", T_TEST, parse_type, false},
	{"-uid", T_TEST, parse_user},
	{"-unique", T_OPTION, parse_unique},
	{"-used", T_TEST, parse_used},
	{"-user", T_TEST, parse_user},
	{"-version", T_ACTION, parse_version},
	{"-warn", T_OPTION, parse_warn, true},
	{"-wholename", T_TEST, parse_path, false},
	{"-writable", T_TEST, parse_access, W_OK},
	{"-x", T_FLAG, parse_xdev},
	{"-xattr", T_TEST, parse_xattr},
	{"-xattrname", T_TEST, parse_xattrname},
	{"-xdev", T_OPTION, parse_xdev},
	{"-xtype", T_TEST, parse_type, true},
	{0},
};

/** Look up an argument in the parse table. */
static const struct table_entry *table_lookup(const char *arg) {
	for (const struct table_entry *entry = parse_table; entry->arg; ++entry) {
		bool match;
		if (entry->prefix) {
			match = strncmp(arg, entry->arg, strlen(entry->arg)) == 0;
		} else {
			match = strcmp(arg, entry->arg) == 0;
		}
		if (match) {
			return entry;
		}
	}

	return NULL;
}

/** Search for a fuzzy match in the parse table. */
static const struct table_entry *table_lookup_fuzzy(const char *arg) {
	const struct table_entry *best = NULL;
	int best_dist = INT_MAX;

	for (const struct table_entry *entry = parse_table; entry->arg; ++entry) {
		int dist = typo_distance(arg, entry->arg);
		if (!best || dist < best_dist) {
			best = entry;
			best_dist = dist;
		}
	}

	return best;
}

/**
 * PRIMARY : OPTION
 *         | TEST
 *         | ACTION
 */
static struct bfs_expr *parse_primary(struct bfs_parser *parser) {
	// Paths are already skipped at this point
	const char *arg = parser->argv[0];

	if (arg[0] != '-') {
		goto unexpected;
	}

	const struct table_entry *match = table_lookup(arg);
	if (match) {
		if (match->parse) {
			goto matched;
		} else {
			goto unexpected;
		}
	}

	match = table_lookup_fuzzy(arg);

	CFILE *cerr = parser->ctx->cerr;
	parse_error(parser, "Unknown argument; did you mean ");
	switch (match->type) {
	case T_FLAG:
		cfprintf(cerr, "${cyn}%s${rs}?", match->arg);
		break;
	case T_OPERATOR:
		cfprintf(cerr, "${red}%s${rs}?", match->arg);
		break;
	default:
		cfprintf(cerr, "${blu}%s${rs}?", match->arg);
		break;
	}

	if (!parser->interactive || !match->parse) {
		fprintf(stderr, "\n");
		goto unmatched;
	}

	fprintf(stderr, " ");
	if (ynprompt() <= 0) {
		goto unmatched;
	}

	fprintf(stderr, "\n");
	parser->argv[0] = match->arg;

matched:
	return match->parse(parser, match->arg1, match->arg2);

unmatched:
	return NULL;

unexpected:
	parse_error(parser, "Expected a predicate.\n");
	return NULL;
}

/**
 * FACTOR : "(" EXPR ")"
 *        | "!" FACTOR | "-not" FACTOR
 *        | "-exclude" FACTOR
 *        | PRIMARY
 */
static struct bfs_expr *parse_factor(struct bfs_parser *parser) {
	if (skip_paths(parser) != 0) {
		return NULL;
	}

	const char *arg = parser->argv[0];
	if (!arg) {
		parse_argv_error(parser, parser->last_arg, 1, "Expression terminated prematurely here.\n");
		return NULL;
	}

	if (strcmp(arg, "(") == 0) {
		parser_advance(parser, T_OPERATOR, 1);

		struct bfs_expr *expr = parse_expr(parser);
		if (!expr) {
			return NULL;
		}

		if (skip_paths(parser) != 0) {
			return NULL;
		}

		arg = parser->argv[0];
		if (!arg || strcmp(arg, ")") != 0) {
			parse_argv_error(parser, parser->last_arg, 1, "Expected a ${red})${rs}.\n");
			return NULL;
		}

		parser_advance(parser, T_OPERATOR, 1);
		return expr;
	} else if (strcmp(arg, "-exclude") == 0) {
		if (parser->excluding) {
			parse_error(parser, "${err}%s${rs} is not supported within ${red}-exclude${rs}.\n", arg);
			return NULL;
		}

		char **argv = parser_advance(parser, T_OPERATOR, 1);
		parser->excluding = true;

		struct bfs_expr *factor = parse_factor(parser);
		if (!factor) {
			return NULL;
		}

		parser->excluding = false;

		bfs_expr_append(parser->ctx->exclude, factor);
		return parse_new_expr(parser, eval_true, parser->argv - argv, argv);
	} else if (strcmp(arg, "!") == 0 || strcmp(arg, "-not") == 0) {
		char **argv = parser_advance(parser, T_OPERATOR, 1);

		struct bfs_expr *factor = parse_factor(parser);
		if (!factor) {
			return NULL;
		}

		return new_unary_expr(parser, eval_not, factor, argv);
	} else {
		return parse_primary(parser);
	}
}

/**
 * TERM : FACTOR
 *      | TERM FACTOR
 *      | TERM "-a" FACTOR
 *      | TERM "-and" FACTOR
 */
static struct bfs_expr *parse_term(struct bfs_parser *parser) {
	struct bfs_expr *term = parse_factor(parser);

	while (term) {
		if (skip_paths(parser) != 0) {
			return NULL;
		}

		const char *arg = parser->argv[0];
		if (!arg) {
			break;
		}

		if (strcmp(arg, "-o") == 0 || strcmp(arg, "-or") == 0
		    || strcmp(arg, ",") == 0
		    || strcmp(arg, ")") == 0) {
			break;
		}

		char **argv = &fake_and_arg;
		if (strcmp(arg, "-a") == 0 || strcmp(arg, "-and") == 0) {
			argv = parser_advance(parser, T_OPERATOR, 1);
		}

		struct bfs_expr *lhs = term;
		struct bfs_expr *rhs = parse_factor(parser);
		if (!rhs) {
			return NULL;
		}

		term = new_binary_expr(parser, eval_and, lhs, rhs, argv);
	}

	return term;
}

/**
 * CLAUSE : TERM
 *        | CLAUSE "-o" TERM
 *        | CLAUSE "-or" TERM
 */
static struct bfs_expr *parse_clause(struct bfs_parser *parser) {
	struct bfs_expr *clause = parse_term(parser);

	while (clause) {
		if (skip_paths(parser) != 0) {
			return NULL;
		}

		const char *arg = parser->argv[0];
		if (!arg) {
			break;
		}

		if (strcmp(arg, "-o") != 0 && strcmp(arg, "-or") != 0) {
			break;
		}

		char **argv = parser_advance(parser, T_OPERATOR, 1);

		struct bfs_expr *lhs = clause;
		struct bfs_expr *rhs = parse_term(parser);
		if (!rhs) {
			return NULL;
		}

		clause = new_binary_expr(parser, eval_or, lhs, rhs, argv);
	}

	return clause;
}

/**
 * EXPR : CLAUSE
 *      | EXPR "," CLAUSE
 */
static struct bfs_expr *parse_expr(struct bfs_parser *parser) {
	struct bfs_expr *expr = parse_clause(parser);

	while (expr) {
		if (skip_paths(parser) != 0) {
			return NULL;
		}

		const char *arg = parser->argv[0];
		if (!arg) {
			break;
		}

		if (strcmp(arg, ",") != 0) {
			break;
		}

		char **argv = parser_advance(parser, T_OPERATOR, 1);

		struct bfs_expr *lhs = expr;
		struct bfs_expr *rhs = parse_clause(parser);
		if (!rhs) {
			return NULL;
		}

		expr = new_binary_expr(parser, eval_comma, lhs, rhs, argv);
	}

	return expr;
}

/**
 * Parse the top-level expression.
 */
static struct bfs_expr *parse_whole_expr(struct bfs_parser *parser) {
	if (skip_paths(parser) != 0) {
		return NULL;
	}

	struct bfs_expr *expr;
	if (parser->argv[0]) {
		expr = parse_expr(parser);
	} else {
		expr = parse_new_expr(parser, eval_true, 1, &fake_true_arg);
	}
	if (!expr) {
		return NULL;
	}

	if (parser->argv[0]) {
		parse_error(parser, "Unexpected argument.\n");
		return NULL;
	}

	if (parser->implicit_print) {
		struct bfs_expr *print = parse_new_expr(parser, eval_fprint, 1, &fake_print_arg);
		if (!print) {
			return NULL;
		}
		init_print_expr(parser, print);

		expr = new_binary_expr(parser, eval_and, expr, print, &fake_and_arg);
		if (!expr) {
			return NULL;
		}
	}

	if (parser->mount_arg && parser->xdev_arg) {
		parse_conflict_warning(parser, parser->mount_arg, 1, parser->xdev_arg, 1,
			"${blu}%s${rs} is redundant in the presence of ${blu}%s${rs}.\n\n",
			parser->xdev_arg[0], parser->mount_arg[0]);
	}

	if (parser->ctx->warn && parser->depth_arg && parser->prune_arg) {
		parse_conflict_warning(parser, parser->depth_arg, 1, parser->prune_arg, 1,
			"${blu}%s${rs} does not work in the presence of ${blu}%s${rs}.\n",
			parser->prune_arg[0], parser->depth_arg[0]);

		if (parser->interactive) {
			bfs_warning(parser->ctx, "Do you want to continue? ");
			if (ynprompt() == 0) {
				return NULL;
			}
		}

		fprintf(stderr, "\n");
	}

	if (parser->ok_expr && parser->files0_stdin_arg) {
		parse_conflict_error(parser, parser->ok_expr->argv, parser->ok_expr->argc, parser->files0_stdin_arg, 2,
			"${blu}%s${rs} conflicts with ${blu}%s${rs} ${bld}%s${rs}.\n",
			parser->ok_expr->argv[0], parser->files0_stdin_arg[0], parser->files0_stdin_arg[1]);
		return NULL;
	}

	return expr;
}

static const char *bftw_strategy_name(enum bftw_strategy strategy) {
	switch (strategy) {
	case BFTW_BFS:
		return "bfs";
	case BFTW_DFS:
		return "dfs";
	case BFTW_IDS:
		return "ids";
	case BFTW_EDS:
		return "eds";
	}

	bfs_bug("Invalid strategy");
	return "???";
}

static void dump_expr_multiline(const struct bfs_ctx *ctx, enum debug_flags flag, const struct bfs_expr *expr, int indent, int rparens) {
	bfs_debug_prefix(ctx, flag);

	for (int i = 0; i < indent; ++i) {
		cfprintf(ctx->cerr, "  ");
	}

	bool close = true;

	if (bfs_expr_is_parent(expr)) {
		if (SLIST_EMPTY(&expr->children)) {
			cfprintf(ctx->cerr, "(${red}%s${rs}", expr->argv[0]);
			++rparens;
		} else {
			cfprintf(ctx->cerr, "(${red}%s${rs}\n", expr->argv[0]);
			for (struct bfs_expr *child = bfs_expr_children(expr); child; child = child->next) {
				int parens = child->next ? 0 : rparens + 1;
				dump_expr_multiline(ctx, flag, child, indent + 1, parens);
			}
			close = false;
		}
	} else {
		if (flag == DEBUG_RATES) {
			cfprintf(ctx->cerr, "%pE", expr);
		} else {
			cfprintf(ctx->cerr, "%pe", expr);
		}
	}

	if (close) {
		for (int i = 0; i < rparens; ++i) {
			cfprintf(ctx->cerr, ")");
		}
		cfprintf(ctx->cerr, "\n");
	}
}

void bfs_ctx_dump(const struct bfs_ctx *ctx, enum debug_flags flag) {
	if (!bfs_debug_prefix(ctx, flag)) {
		return;
	}

	CFILE *cerr = ctx->cerr;

	cfprintf(cerr, "${ex}%s${rs}", ctx->argv[0]);

	if (ctx->flags & BFTW_FOLLOW_ALL) {
		cfprintf(cerr, " ${cyn}-L${rs}");
	} else if (ctx->flags & BFTW_FOLLOW_ROOTS) {
		cfprintf(cerr, " ${cyn}-H${rs}");
	} else {
		cfprintf(cerr, " ${cyn}-P${rs}");
	}

	if (ctx->xargs_safe) {
		cfprintf(cerr, " ${cyn}-X${rs}");
	}

	if (ctx->flags & BFTW_SORT) {
		cfprintf(cerr, " ${cyn}-s${rs}");
	}

	cfprintf(cerr, " ${cyn}-j${bld}%d${rs}", ctx->threads);

	if (ctx->optlevel != 3) {
		cfprintf(cerr, " ${cyn}-O${bld}%d${rs}", ctx->optlevel);
	}

	cfprintf(cerr, " ${cyn}-S${rs} ${bld}%s${rs}", bftw_strategy_name(ctx->strategy));

	enum debug_flags debug = ctx->debug;
	if (debug == DEBUG_ALL) {
		cfprintf(cerr, " ${cyn}-D${rs} ${bld}all${rs}");
	} else if (debug) {
		cfprintf(cerr, " ${cyn}-D${rs} ");
		for (enum debug_flags i = 1; DEBUG_ALL & i; i <<= 1) {
			if (debug & i) {
				cfprintf(cerr, "${bld}%s${rs}", debug_flag_name(i));
				debug ^= i;
				if (debug) {
					cfprintf(cerr, ",");
				}
			}
		}
	}

	for (size_t i = 0; i < ctx->npaths; ++i) {
		const char *path = ctx->paths[i];
		char c = path[0];
		if (c == '-' || c == '(' || c == ')' || c == '!' || c == ',') {
			cfprintf(cerr, " ${cyn}-f${rs}");
		}
		cfprintf(cerr, " ${mag}%pq${rs}", path);
	}

	if (ctx->cout->colors) {
		cfprintf(cerr, " ${blu}-color${rs}");
	} else {
		cfprintf(cerr, " ${blu}-nocolor${rs}");
	}
	if (ctx->flags & BFTW_POST_ORDER) {
		cfprintf(cerr, " ${blu}-depth${rs}");
	}
	if (ctx->ignore_races) {
		cfprintf(cerr, " ${blu}-ignore_readdir_race${rs}");
	}
	if (ctx->mindepth != 0) {
		cfprintf(cerr, " ${blu}-mindepth${rs} ${bld}%d${rs}", ctx->mindepth);
	}
	if (ctx->maxdepth != INT_MAX) {
		cfprintf(cerr, " ${blu}-maxdepth${rs} ${bld}%d${rs}", ctx->maxdepth);
	}
	if (ctx->flags & BFTW_SKIP_MOUNTS) {
		cfprintf(cerr, " ${blu}-mount${rs}");
	}
	if (ctx->status) {
		cfprintf(cerr, " ${blu}-status${rs}");
	}
	if (ctx->unique) {
		cfprintf(cerr, " ${blu}-unique${rs}");
	}
	if ((ctx->flags & (BFTW_SKIP_MOUNTS | BFTW_PRUNE_MOUNTS)) == BFTW_PRUNE_MOUNTS) {
		cfprintf(cerr, " ${blu}-xdev${rs}");
	}

	fputs("\n", stderr);

	bfs_debug(ctx, flag, "(${red}-exclude${rs}\n");
	dump_expr_multiline(ctx, flag, ctx->exclude, 1, 1);

	dump_expr_multiline(ctx, flag, ctx->expr, 0, 0);
}

/**
 * Dump the estimated costs.
 */
static void dump_costs(const struct bfs_ctx *ctx) {
	const struct bfs_expr *expr = ctx->expr;
	bfs_debug(ctx, DEBUG_COST, "       Cost: ~${ylw}%g${rs}\n", expr->cost);
	bfs_debug(ctx, DEBUG_COST, "Probability: ~${ylw}%g%%${rs}\n", 100.0 * expr->probability);
}

struct bfs_ctx *bfs_parse_cmdline(int argc, char *argv[]) {
	struct bfs_ctx *ctx = bfs_ctx_new();
	if (!ctx) {
		perror("bfs_ctx_new()");
		goto fail;
	}

	static char *default_argv[] = {BFS_COMMAND, NULL};
	if (argc < 1) {
		argc = 1;
		argv = default_argv;
	}

	ctx->argc = argc;
	ctx->argv = xmemdup(argv, sizeof_array(char *, argc + 1));
	if (!ctx->argv) {
		perror("xmemdup()");
		goto fail;
	}

	enum use_color use_color = COLOR_AUTO;
	const char *no_color = getenv("NO_COLOR");
	if (no_color && *no_color) {
		// https://no-color.org/
		use_color = COLOR_NEVER;
	}

	ctx->colors = parse_colors();
	if (!ctx->colors) {
		ctx->colors_error = errno;
	}

	ctx->cerr = cfwrap(stderr, use_color ? ctx->colors : NULL, false);
	if (!ctx->cerr) {
		perror("cfwrap()");
		goto fail;
	}

	ctx->cout = cfwrap(stdout, use_color ? ctx->colors : NULL, false);
	if (!ctx->cout) {
		bfs_perror(ctx, "cfwrap()");
		goto fail;
	}

	if (!bfs_ctx_dedup(ctx, ctx->cout, NULL) || !bfs_ctx_dedup(ctx, ctx->cerr, NULL)) {
		bfs_perror(ctx, "bfs_ctx_dedup()");
		goto fail;
	}

	bool stdin_tty = isatty(STDIN_FILENO);
	bool stdout_tty = isatty(STDOUT_FILENO);
	bool stderr_tty = isatty(STDERR_FILENO);

	if (getenv("POSIXLY_CORRECT")) {
		ctx->posixly_correct = true;
	} else {
		ctx->warn = stdin_tty;
	}

	struct bfs_parser parser = {
		.ctx = ctx,
		.argv = ctx->argv + 1,
		.command = ctx->argv[0],
		.regex_type = BFS_REGEX_POSIX_BASIC,
		.stdout_tty = stdout_tty,
		.interactive = stdin_tty && stderr_tty,
		.use_color = use_color,
		.implicit_print = true,
		.implicit_root = true,
		.just_info = false,
		.excluding = false,
		.last_arg = NULL,
		.depth_arg = NULL,
		.prune_arg = NULL,
		.mount_arg = NULL,
		.xdev_arg = NULL,
		.files0_stdin_arg = NULL,
		.ok_expr = NULL,
		.now = ctx->now,
	};

	ctx->exclude = parse_new_expr(&parser, eval_or, 1, &fake_or_arg);
	if (!ctx->exclude) {
		goto fail;
	}

	ctx->expr = parse_whole_expr(&parser);
	if (!ctx->expr) {
		if (parser.just_info) {
			goto done;
		} else {
			goto fail;
		}
	}

	if (parser.use_color == COLOR_AUTO && !ctx->colors) {
		bfs_warning(ctx, "Error parsing $$LS_COLORS: %s.\n\n", xstrerror(ctx->colors_error));
	}

	if (bfs_optimize(ctx) != 0) {
		bfs_perror(ctx, "bfs_optimize()");
		goto fail;
	}

	if (ctx->npaths == 0 && parser.implicit_root) {
		if (parse_root(&parser, ".") != 0) {
			goto fail;
		}
	}

	if ((ctx->flags & BFTW_FOLLOW_ALL) && !ctx->unique) {
		// We need bftw() to detect cycles unless -unique does it for us
		ctx->flags |= BFTW_DETECT_CYCLES;
	}

	bfs_ctx_dump(ctx, DEBUG_TREE);
	dump_costs(ctx);

done:
	return ctx;

fail:
	bfs_ctx_free(ctx);
	return NULL;
}
