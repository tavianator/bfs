/****************************************************************************
 * bfs                                                                      *
 * Copyright (C) 2015-2022 Tavian Barnes <tavianator@tavianator.com>        *
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
 * The command line parser.  Expressions are parsed by recursive descent, with a
 * grammar described in the comments of the parse_*() functions.  The parser
 * also accepts flags and paths at any point in the expression, by treating
 * flags like always-true options, and skipping over paths wherever they appear.
 */

#include "parse.h"
#include "bfs.h"
#include "bftw.h"
#include "color.h"
#include "ctx.h"
#include "darray.h"
#include "diag.h"
#include "dir.h"
#include "eval.h"
#include "exec.h"
#include "expr.h"
#include "fsade.h"
#include "opt.h"
#include "printf.h"
#include "pwcache.h"
#include "stat.h"
#include "typo.h"
#include "util.h"
#include "xregex.h"
#include "xspawn.h"
#include "xtime.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <grp.h>
#include <limits.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

// Strings printed by -D tree for "fake" expressions
static char *fake_and_arg = "-a";
static char *fake_false_arg = "-false";
static char *fake_hidden_arg = "-hidden";
static char *fake_or_arg = "-o";
static char *fake_print_arg = "-print";
static char *fake_true_arg = "-true";

// Cost estimation constants
#define FAST_COST     40.0
#define STAT_COST   1000.0
#define PRINT_COST 20000.0

struct bfs_expr bfs_true = {
	.eval_fn = eval_true,
	.argc = 1,
	.argv = &fake_true_arg,
	.pure = true,
	.always_true = true,
	.cost = FAST_COST,
	.probability = 1.0,
};

struct bfs_expr bfs_false = {
	.eval_fn = eval_false,
	.argc = 1,
	.argv = &fake_false_arg,
	.pure = true,
	.always_false = true,
	.cost = FAST_COST,
	.probability = 0.0,
};

void bfs_expr_free(struct bfs_expr *expr) {
	if (!expr || expr == &bfs_true || expr == &bfs_false) {
		return;
	}

	if (bfs_expr_has_children(expr)) {
		bfs_expr_free(expr->rhs);
		bfs_expr_free(expr->lhs);
	} else if (expr->eval_fn == eval_exec) {
		bfs_exec_free(expr->exec);
	} else if (expr->eval_fn == eval_fprintf) {
		bfs_printf_free(expr->printf);
	} else if (expr->eval_fn == eval_regex) {
		bfs_regfree(expr->regex);
	}

	free(expr);
}

struct bfs_expr *bfs_expr_new(bfs_eval_fn *eval_fn, size_t argc, char **argv) {
	struct bfs_expr *expr = malloc(sizeof(*expr));
	if (!expr) {
		perror("malloc()");
		return NULL;
	}

	expr->eval_fn = eval_fn;
	expr->argc = argc;
	expr->argv = argv;
	expr->persistent_fds = 0;
	expr->ephemeral_fds = 0;
	expr->pure = false;
	expr->always_true = false;
	expr->always_false = false;
	expr->cost = FAST_COST;
	expr->probability = 0.5;
	expr->evaluations = 0;
	expr->successes = 0;
	expr->elapsed.tv_sec = 0;
	expr->elapsed.tv_nsec = 0;
	return expr;
}

/**
 * Create a new unary expression.
 */
static struct bfs_expr *new_unary_expr(bfs_eval_fn *eval_fn, struct bfs_expr *rhs, char **argv) {
	struct bfs_expr *expr = bfs_expr_new(eval_fn, 1, argv);
	if (!expr) {
		bfs_expr_free(rhs);
		return NULL;
	}

	expr->lhs = NULL;
	expr->rhs = rhs;
	assert(bfs_expr_has_children(expr));

	expr->persistent_fds = rhs->persistent_fds;
	expr->ephemeral_fds = rhs->ephemeral_fds;
	return expr;
}

/**
 * Create a new binary expression.
 */
static struct bfs_expr *new_binary_expr(bfs_eval_fn *eval_fn, struct bfs_expr *lhs, struct bfs_expr *rhs, char **argv) {
	struct bfs_expr *expr = bfs_expr_new(eval_fn, 1, argv);
	if (!expr) {
		bfs_expr_free(rhs);
		bfs_expr_free(lhs);
		return NULL;
	}

	expr->lhs = lhs;
	expr->rhs = rhs;
	assert(bfs_expr_has_children(expr));

	expr->persistent_fds = lhs->persistent_fds + rhs->persistent_fds;
	if (lhs->ephemeral_fds > rhs->ephemeral_fds) {
		expr->ephemeral_fds = lhs->ephemeral_fds;
	} else {
		expr->ephemeral_fds = rhs->ephemeral_fds;
	}

	return expr;
}

bool bfs_expr_has_children(const struct bfs_expr *expr) {
	return expr->eval_fn == eval_and
		|| expr->eval_fn == eval_or
		|| expr->eval_fn == eval_not
		|| expr->eval_fn == eval_comma;
}

bool bfs_expr_never_returns(const struct bfs_expr *expr) {
	// Expressions that never return are vacuously both always true and always false
	return expr->always_true && expr->always_false;
}

/**
 * Set an expression to always return true.
 */
static void expr_set_always_true(struct bfs_expr *expr) {
	expr->always_true = true;
	expr->probability = 1.0;
}

/**
 * Set an expression to never return.
 */
static void expr_set_never_returns(struct bfs_expr *expr) {
	expr->always_true = expr->always_false = true;
}

/**
 * Color use flags.
 */
enum use_color {
	COLOR_NEVER,
	COLOR_AUTO,
	COLOR_ALWAYS,
};

/**
 * Ephemeral state for parsing the command line.
 */
struct parser_state {
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
	/** Whether stdin has been consumed by -files0-from -. */
	bool stdin_consumed;
	/** Whether -color or -nocolor has been passed. */
	enum use_color use_color;
	/** Whether a -print action is implied. */
	bool implicit_print;
	/** Whether the default root "." should be used. */
	bool implicit_root;
	/** Whether the expression has started. */
	bool expr_started;
	/** Whether any non-option arguments have been encountered. */
	bool non_option_seen;
	/** Whether an information option like -help or -version was passed. */
	bool just_info;
	/** Whether we are currently parsing an -exclude expression. */
	bool excluding;

	/** The last non-path argument. */
	const char *last_arg;
	/** A "-depth"-type argument if any. */
	const char *depth_arg;
	/** A "-prune"-type argument if any. */
	const char *prune_arg;
	/** A "-mount"-type argument if any. */
	const char *mount_arg;
	/** An "-xdev"-type argument if any. */
	const char *xdev_arg;
	/** An "-ok"-type argument if any. */
	const char *ok_arg;

	/** The current time. */
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
 * Print an error message during parsing.
 */
BFS_FORMATTER(2, 3)
static void parse_error(const struct parser_state *state, const char *format, ...) {
	va_list args;
	va_start(args, format);
	bfs_verror(state->ctx, format, args);
	va_end(args);
}

/**
 * Print a low-level error message during parsing.
 */
static void parse_perror(const struct parser_state *state, const char *str) {
	bfs_perror(state->ctx, str);
}

/**
 * Print a warning message during parsing.
 */
BFS_FORMATTER(2, 3)
static bool parse_warning(const struct parser_state *state, const char *format, ...) {
	va_list args;
	va_start(args, format);
	bool ret = bfs_vwarning(state->ctx, format, args);
	va_end(args);
	return ret;
}

/**
 * Fill in a "-print"-type expression.
 */
static void init_print_expr(struct parser_state *state, struct bfs_expr *expr) {
	expr_set_always_true(expr);
	expr->cost = PRINT_COST;
	expr->cfile = state->ctx->cout;
}

/**
 * Open a file for an expression.
 */
static int expr_open(struct parser_state *state, struct bfs_expr *expr, const char *path) {
	struct bfs_ctx *ctx = state->ctx;

	FILE *file = NULL;
	CFILE *cfile = NULL;

	file = xfopen(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC);
	if (!file) {
		goto fail;
	}

	cfile = cfwrap(file, state->use_color ? ctx->colors : NULL, true);
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
	return 0;

fail:
	parse_error(state, "${blu}%s${rs} ${bld}%s${rs}: %m.\n", expr->argv[0], path);
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
static int stat_arg(const struct parser_state *state, struct bfs_expr *expr, const char *path, struct bfs_stat *sb) {
	const struct bfs_ctx *ctx = state->ctx;

	bool follow = ctx->flags & (BFTW_FOLLOW_ROOTS | BFTW_FOLLOW_ALL);
	enum bfs_stat_flags flags = follow ? BFS_STAT_TRYFOLLOW : BFS_STAT_NOFOLLOW;

	int ret = bfs_stat(AT_FDCWD, path, flags, sb);
	if (ret != 0) {
		parse_error(state, "${blu}%s${rs} ${bld}%s${rs}: %m.\n", expr->argv[0], path);
	}
	return ret;
}

/**
 * Parse the expression specified on the command line.
 */
static struct bfs_expr *parse_expr(struct parser_state *state);

/**
 * Advance by a single token.
 */
static char **parser_advance(struct parser_state *state, enum token_type type, size_t argc) {
	if (type != T_FLAG && type != T_PATH) {
		state->expr_started = true;

		if (type != T_OPTION) {
			state->non_option_seen = true;
		}
	}

	if (type != T_PATH) {
		state->last_arg = *state->argv;
	}

	char **argv = state->argv;
	state->argv += argc;
	return argv;
}

/**
 * Parse a root path.
 */
static int parse_root(struct parser_state *state, const char *path) {
	char *copy = strdup(path);
	if (!copy) {
		parse_perror(state, "strdup()");
		return -1;
	}

	struct bfs_ctx *ctx = state->ctx;
	if (DARRAY_PUSH(&ctx->paths, &copy) != 0) {
		parse_perror(state, "DARRAY_PUSH()");
		free(copy);
		return -1;
	}

	state->implicit_root = false;
	return 0;
}

/**
 * While parsing an expression, skip any paths and add them to ctx->paths.
 */
static int skip_paths(struct parser_state *state) {
	while (true) {
		const char *arg = state->argv[0];
		if (!arg) {
			return 0;
		}

		if (arg[0] == '-') {
			if (strcmp(arg, "--") == 0) {
				// find uses -- to separate flags from the rest
				// of the command line.  We allow mixing flags
				// and paths/predicates, so we just ignore --.
				parser_advance(state, T_FLAG, 1);
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

		if (state->expr_started) {
			// By POSIX, these can be paths.  We only treat them as
			// such at the beginning of the command line.
			if (strcmp(arg, ")") == 0 || strcmp(arg, ",") == 0) {
				return 0;
			}
		}

		if (parse_root(state, arg) != 0) {
			return -1;
		}

		parser_advance(state, T_PATH, 1);
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
static const char *parse_int(const struct parser_state *state, const char *str, void *result, enum int_flags flags) {
	char *endptr;

	int base = flags & IF_BASE_MASK;
	if (base == 0) {
		base = 10;
	}

	errno = 0;
	long long value = strtoll(str, &endptr, base);
	if (errno != 0) {
		if (errno == ERANGE) {
			goto range;
		} else {
			goto bad;
		}
	}

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
		assert(!"Invalid int size");
		goto bad;
	}

	return endptr;

bad:
	if (!(flags & IF_QUIET)) {
		parse_error(state, "${bld}%s${rs} is not a valid integer.\n", str);
	}
	return NULL;

negative:
	if (!(flags & IF_QUIET)) {
		parse_error(state, "Negative integer ${bld}%s${rs} is not allowed here.\n", str);
	}
	return NULL;

range:
	if (!(flags & IF_QUIET)) {
		parse_error(state, "${bld}%s${rs} is too large an integer.\n", str);
	}
	return NULL;
}

/**
 * Parse an integer and a comparison flag.
 */
static const char *parse_icmp(const struct parser_state *state, const char *str, struct bfs_expr *expr, enum int_flags flags) {
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

	return parse_int(state, str, &expr->num, flags | IF_LONG_LONG | IF_UNSIGNED);
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
static struct bfs_expr *parse_flag(struct parser_state *state, size_t argc) {
	parser_advance(state, T_FLAG, argc);
	return &bfs_true;
}

/**
 * Parse a flag that doesn't take a value.
 */
static struct bfs_expr *parse_nullary_flag(struct parser_state *state) {
	return parse_flag(state, 1);
}

/**
 * Parse a flag that takes a single value.
 */
static struct bfs_expr *parse_unary_flag(struct parser_state *state) {
	return parse_flag(state, 2);
}

/**
 * Parse a single option.
 */
static struct bfs_expr *parse_option(struct parser_state *state, size_t argc) {
	const char *arg = *parser_advance(state, T_OPTION, argc);

	if (state->non_option_seen) {
		parse_warning(state,
		              "The ${blu}%s${rs} option applies to the entire command line.  For clarity, place\n"
		              "it before any non-option arguments.\n\n",
		              arg);
	}

	return &bfs_true;
}

/**
 * Parse an option that doesn't take a value.
 */
static struct bfs_expr *parse_nullary_option(struct parser_state *state) {
	return parse_option(state, 1);
}

/**
 * Parse an option that takes a value.
 */
static struct bfs_expr *parse_unary_option(struct parser_state *state) {
	return parse_option(state, 2);
}

/**
 * Parse a single positional option.
 */
static struct bfs_expr *parse_positional_option(struct parser_state *state, size_t argc) {
	parser_advance(state, T_OPTION, argc);
	return &bfs_true;
}

/**
 * Parse a positional option that doesn't take a value.
 */
static struct bfs_expr *parse_nullary_positional_option(struct parser_state *state) {
	return parse_positional_option(state, 1);
}

/**
 * Parse a positional option that takes a single value.
 */
static struct bfs_expr *parse_unary_positional_option(struct parser_state *state) {
	return parse_positional_option(state, 2);
}

/**
 * Parse a single test.
 */
static struct bfs_expr *parse_test(struct parser_state *state, bfs_eval_fn *eval_fn, size_t argc) {
	char **argv = parser_advance(state, T_TEST, argc);
	struct bfs_expr *expr = bfs_expr_new(eval_fn, argc, argv);
	if (expr) {
		expr->pure = true;
	}
	return expr;
}

/**
 * Parse a test that doesn't take a value.
 */
static struct bfs_expr *parse_nullary_test(struct parser_state *state, bfs_eval_fn *eval_fn) {
	return parse_test(state, eval_fn, 1);
}

/**
 * Parse a test that takes a value.
 */
static struct bfs_expr *parse_unary_test(struct parser_state *state, bfs_eval_fn *eval_fn) {
	const char *arg = state->argv[0];
	const char *value = state->argv[1];
	if (!value) {
		parse_error(state, "${blu}%s${rs} needs a value.\n", arg);
		return NULL;
	}

	return parse_test(state, eval_fn, 2);
}

/**
 * Parse a single action.
 */
static struct bfs_expr *parse_action(struct parser_state *state, bfs_eval_fn *eval_fn, size_t argc) {
	char **argv = state->argv;

	if (state->excluding) {
		parse_error(state, "The ${blu}%s${rs} action is not supported within ${red}-exclude${rs}.\n", argv[0]);
		return NULL;
	}

	if (eval_fn != eval_prune && eval_fn != eval_quit) {
		state->implicit_print = false;
	}

	parser_advance(state, T_ACTION, argc);
	return bfs_expr_new(eval_fn, argc, argv);
}

/**
 * Parse an action that takes no arguments.
 */
static struct bfs_expr *parse_nullary_action(struct parser_state *state, bfs_eval_fn *eval_fn) {
	return parse_action(state, eval_fn, 1);
}

/**
 * Parse an action that takes one argument.
 */
static struct bfs_expr *parse_unary_action(struct parser_state *state, bfs_eval_fn *eval_fn) {
	const char *arg = state->argv[0];
	const char *value = state->argv[1];
	if (!value) {
		parse_error(state, "${blu}%s${rs} needs a value.\n", arg);
		return NULL;
	}

	return parse_action(state, eval_fn, 2);
}

/**
 * Parse a test expression with integer data and a comparison flag.
 */
static struct bfs_expr *parse_test_icmp(struct parser_state *state, bfs_eval_fn *eval_fn) {
	struct bfs_expr *expr = parse_unary_test(state, eval_fn);
	if (!expr) {
		return NULL;
	}

	if (!parse_icmp(state, expr->argv[1], expr, 0)) {
		bfs_expr_free(expr);
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
static struct bfs_expr *parse_debug(struct parser_state *state, int arg1, int arg2) {
	struct bfs_ctx *ctx = state->ctx;

	const char *arg = state->argv[0];
	const char *flags = state->argv[1];
	if (!flags) {
		parse_error(state, "${cyn}%s${rs} needs a flag.\n\n", arg);
		debug_help(ctx->cerr);
		return NULL;
	}

	bool unrecognized = false;

	for (const char *flag = flags, *next; flag; flag = next) {
		size_t len = strcspn(flag, ",");
		if (flag[len]) {
			next = flag + len + 1;
		} else {
			next = NULL;
		}

		if (parse_debug_flag(flag, len, "help")) {
			debug_help(ctx->cout);
			state->just_info = true;
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
			if (parse_warning(state, "Unrecognized debug flag ${bld}")) {
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

	return parse_unary_flag(state);
}

/**
 * Parse -On.
 */
static struct bfs_expr *parse_optlevel(struct parser_state *state, int arg1, int arg2) {
	int *optlevel = &state->ctx->optlevel;

	if (strcmp(state->argv[0], "-Ofast") == 0) {
		*optlevel = 4;
	} else if (!parse_int(state, state->argv[0] + 2, optlevel, IF_INT | IF_UNSIGNED)) {
		return NULL;
	}

	if (*optlevel > 4) {
		parse_warning(state, "${cyn}-O${bld}%s${rs} is the same as ${cyn}-O${bld}4${rs}.\n\n", state->argv[0] + 2);
	}

	return parse_nullary_flag(state);
}

/**
 * Parse -[PHL], -(no)?follow.
 */
static struct bfs_expr *parse_follow(struct parser_state *state, int flags, int option) {
	struct bfs_ctx *ctx = state->ctx;
	ctx->flags &= ~(BFTW_FOLLOW_ROOTS | BFTW_FOLLOW_ALL);
	ctx->flags |= flags;
	if (option) {
		return parse_nullary_positional_option(state);
	} else {
		return parse_nullary_flag(state);
	}
}

/**
 * Parse -X.
 */
static struct bfs_expr *parse_xargs_safe(struct parser_state *state, int arg1, int arg2) {
	state->ctx->xargs_safe = true;
	return parse_nullary_flag(state);
}

/**
 * Parse -executable, -readable, -writable
 */
static struct bfs_expr *parse_access(struct parser_state *state, int flag, int arg2) {
	struct bfs_expr *expr = parse_nullary_test(state, eval_access);
	if (!expr) {
		return NULL;
	}

	expr->num = flag;
	expr->cost = STAT_COST;

	switch (flag) {
	case R_OK:
		expr->probability = 0.99;
		break;
	case W_OK:
		expr->probability = 0.8;
		break;
	case X_OK:
		expr->probability = 0.2;
		break;
	}

	return expr;
}

/**
 * Parse -acl.
 */
static struct bfs_expr *parse_acl(struct parser_state *state, int flag, int arg2) {
#if BFS_CAN_CHECK_ACL
	struct bfs_expr *expr = parse_nullary_test(state, eval_acl);
	if (expr) {
		expr->cost = STAT_COST;
		expr->probability = 0.00002;
	}
	return expr;
#else
	parse_error(state, "${blu}%s${rs} is missing platform support.\n", state->argv[0]);
	return NULL;
#endif
}

/**
 * Parse -[aBcm]?newer.
 */
static struct bfs_expr *parse_newer(struct parser_state *state, int field, int arg2) {
	struct bfs_expr *expr = parse_unary_test(state, eval_newer);
	if (!expr) {
		return NULL;
	}

	struct bfs_stat sb;
	if (stat_arg(state, expr, expr->argv[1], &sb) != 0) {
		goto fail;
	}

	expr->cost = STAT_COST;
	expr->reftime = sb.mtime;
	expr->stat_field = field;
	return expr;

fail:
	bfs_expr_free(expr);
	return NULL;
}

/**
 * Parse -[aBcm]min.
 */
static struct bfs_expr *parse_min(struct parser_state *state, int field, int arg2) {
	struct bfs_expr *expr = parse_test_icmp(state, eval_time);
	if (!expr) {
		return NULL;
	}

	expr->cost = STAT_COST;
	expr->reftime = state->now;
	expr->stat_field = field;
	expr->time_unit = BFS_MINUTES;
	return expr;
}

/**
 * Parse -[aBcm]time.
 */
static struct bfs_expr *parse_time(struct parser_state *state, int field, int arg2) {
	struct bfs_expr *expr = parse_unary_test(state, eval_time);
	if (!expr) {
		return NULL;
	}

	expr->cost = STAT_COST;
	expr->reftime = state->now;
	expr->stat_field = field;

	const char *tail = parse_icmp(state, expr->argv[1], expr, IF_PARTIAL_OK);
	if (!tail) {
		goto fail;
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
			BFS_FALLTHROUGH;
		case 'd':
			time *= 24;
			BFS_FALLTHROUGH;
		case 'h':
			time *= 60;
			BFS_FALLTHROUGH;
		case 'm':
			time *= 60;
			BFS_FALLTHROUGH;
		case 's':
			break;
		default:
			parse_error(state, "${blu}%s${rs} ${bld}%s${rs}: Unknown time unit ${bld}%c${rs}.\n",
			            expr->argv[0], expr->argv[1], *tail);
			goto fail;
		}

		expr->num += time;

		if (!*++tail) {
			break;
		}

		tail = parse_int(state, tail, &time, IF_PARTIAL_OK | IF_LONG_LONG | IF_UNSIGNED);
		if (!tail) {
			goto fail;
		}
		if (!*tail) {
			parse_error(state, "${blu}%s${rs} ${bld}%s${rs}: Missing time unit.\n",
			            expr->argv[0], expr->argv[1]);
			goto fail;
		}
	}

	expr->time_unit = BFS_SECONDS;
	return expr;

fail:
	bfs_expr_free(expr);
	return NULL;
}

/**
 * Parse -capable.
 */
static struct bfs_expr *parse_capable(struct parser_state *state, int flag, int arg2) {
#if BFS_CAN_CHECK_CAPABILITIES
	struct bfs_expr *expr = parse_nullary_test(state, eval_capable);
	if (expr) {
		expr->cost = STAT_COST;
		expr->probability = 0.000002;
	}
	return expr;
#else
	parse_error(state, "${blu}%s${rs} is missing platform support.\n", state->argv[0]);
	return NULL;
#endif
}

/**
 * Parse -(no)?color.
 */
static struct bfs_expr *parse_color(struct parser_state *state, int color, int arg2) {
	struct bfs_ctx *ctx = state->ctx;
	struct colors *colors = ctx->colors;

	if (color) {
		if (!colors) {
			parse_error(state, "${blu}%s${rs}: %s.\n", state->argv[0], strerror(ctx->colors_error));
			return NULL;
		}

		state->use_color = COLOR_ALWAYS;
		ctx->cout->colors = colors;
		ctx->cerr->colors = colors;
	} else {
		state->use_color = COLOR_NEVER;
		ctx->cout->colors = NULL;
		ctx->cerr->colors = NULL;
	}

	return parse_nullary_option(state);
}

/**
 * Parse -{false,true}.
 */
static struct bfs_expr *parse_const(struct parser_state *state, int value, int arg2) {
	parser_advance(state, T_TEST, 1);
	return value ? &bfs_true : &bfs_false;
}

/**
 * Parse -daystart.
 */
static struct bfs_expr *parse_daystart(struct parser_state *state, int arg1, int arg2) {
	struct tm tm;
	if (xlocaltime(&state->now.tv_sec, &tm) != 0) {
		parse_perror(state, "xlocaltime()");
		return NULL;
	}

	if (tm.tm_hour || tm.tm_min || tm.tm_sec || state->now.tv_nsec) {
		++tm.tm_mday;
	}
	tm.tm_hour = 0;
	tm.tm_min = 0;
	tm.tm_sec = 0;

	time_t time;
	if (xmktime(&tm, &time) != 0) {
		parse_perror(state, "xmktime()");
		return NULL;
	}

	state->now.tv_sec = time;
	state->now.tv_nsec = 0;

	return parse_nullary_positional_option(state);
}

/**
 * Parse -delete.
 */
static struct bfs_expr *parse_delete(struct parser_state *state, int arg1, int arg2) {
	state->ctx->flags |= BFTW_POST_ORDER;
	state->depth_arg = state->argv[0];
	return parse_nullary_action(state, eval_delete);
}

/**
 * Parse -d.
 */
static struct bfs_expr *parse_depth(struct parser_state *state, int arg1, int arg2) {
	state->ctx->flags |= BFTW_POST_ORDER;
	state->depth_arg = state->argv[0];
	return parse_nullary_flag(state);
}

/**
 * Parse -depth [N].
 */
static struct bfs_expr *parse_depth_n(struct parser_state *state, int arg1, int arg2) {
	const char *arg = state->argv[1];
	if (arg && looks_like_icmp(arg)) {
		return parse_test_icmp(state, eval_depth);
	} else {
		return parse_depth(state, arg1, arg2);
	}
}

/**
 * Parse -{min,max}depth N.
 */
static struct bfs_expr *parse_depth_limit(struct parser_state *state, int is_min, int arg2) {
	struct bfs_ctx *ctx = state->ctx;
	const char *arg = state->argv[0];
	const char *value = state->argv[1];
	if (!value) {
		parse_error(state, "${blu}%s${rs} needs a value.\n", arg);
		return NULL;
	}

	int *depth = is_min ? &ctx->mindepth : &ctx->maxdepth;
	if (!parse_int(state, value, depth, IF_INT | IF_UNSIGNED)) {
		return NULL;
	}

	return parse_unary_option(state);
}

/**
 * Parse -empty.
 */
static struct bfs_expr *parse_empty(struct parser_state *state, int arg1, int arg2) {
	struct bfs_expr *expr = parse_nullary_test(state, eval_empty);
	if (!expr) {
		return NULL;
	}

	expr->cost = 2000.0;
	expr->probability = 0.01;

	if (state->ctx->optlevel < 4) {
		// Since -empty attempts to open and read directories, it may
		// have side effects such as reporting permission errors, and
		// thus shouldn't be re-ordered without aggressive optimizations
		expr->pure = false;
	}

	expr->ephemeral_fds = 1;

	return expr;
}

/**
 * Parse -exec(dir)?/-ok(dir)?.
 */
static struct bfs_expr *parse_exec(struct parser_state *state, int flags, int arg2) {
	struct bfs_exec *execbuf = bfs_exec_parse(state->ctx, state->argv, flags);
	if (!execbuf) {
		return NULL;
	}

	struct bfs_expr *expr = parse_action(state, eval_exec, execbuf->tmpl_argc + 2);
	if (!expr) {
		bfs_exec_free(execbuf);
		return NULL;
	}

	expr->exec = execbuf;

	if (execbuf->flags & BFS_EXEC_MULTI) {
		expr_set_always_true(expr);
	} else {
		expr->cost = 1000000.0;
	}

	expr->ephemeral_fds = 2;
	if (execbuf->flags & BFS_EXEC_CHDIR) {
		if (execbuf->flags & BFS_EXEC_MULTI) {
			expr->persistent_fds = 1;
		} else {
			++expr->ephemeral_fds;
		}
	}

	if (execbuf->flags & BFS_EXEC_CONFIRM) {
		state->ok_arg = expr->argv[0];
	}

	return expr;
}

/**
 * Parse -exit [STATUS].
 */
static struct bfs_expr *parse_exit(struct parser_state *state, int arg1, int arg2) {
	size_t argc = 1;
	const char *value = state->argv[1];

	int status = EXIT_SUCCESS;
	if (value && parse_int(state, value, &status, IF_INT | IF_UNSIGNED | IF_QUIET)) {
		argc = 2;
	}

	struct bfs_expr *expr = parse_action(state, eval_exit, argc);
	if (expr) {
		expr_set_never_returns(expr);
		expr->num = status;
	}
	return expr;
}

/**
 * Parse -f PATH.
 */
static struct bfs_expr *parse_f(struct parser_state *state, int arg1, int arg2) {
	parser_advance(state, T_FLAG, 1);

	const char *path = state->argv[0];
	if (!path) {
		parse_error(state, "${cyn}-f${rs} requires a path.\n");
		return NULL;
	}

	if (parse_root(state, path) != 0) {
		return NULL;
	}

	parser_advance(state, T_PATH, 1);
	return &bfs_true;
}

/**
 * Parse -files0-from PATH.
 */
static struct bfs_expr *parse_files0_from(struct parser_state *state, int arg1, int arg2) {
	const char *arg = state->argv[0];
	const char *from = state->argv[1];
	if (!from) {
		parse_error(state, "${blu}%s${rs} requires a path.\n", arg);
		return NULL;
	}

	FILE *file;
	if (strcmp(from, "-") == 0) {
		file = stdin;
	} else {
		file = xfopen(from, O_RDONLY | O_CLOEXEC);
	}
	if (!file) {
		parse_error(state, "${blu}%s${rs} ${bld}%s${rs}: %m.\n", arg, from);
		return NULL;
	}

	struct bfs_expr *expr = parse_unary_positional_option(state);

	while (true) {
		char *path = xgetdelim(file, '\0');
		if (!path) {
			if (errno) {
				parse_error(state, "${blu}%s${rs} ${bld}%s${rs}: %m.\n", arg, from);
				expr = NULL;
			}
			break;
		}

		int ret = parse_root(state, path);
		free(path);
		if (ret != 0) {
			expr = NULL;
			break;
		}
	}

	if (file == stdin) {
		state->stdin_consumed = true;
	} else {
		fclose(file);
	}

	state->implicit_root = false;
	return expr;
}

/**
 * Parse -flags FLAGS.
 */
static struct bfs_expr *parse_flags(struct parser_state *state, int arg1, int arg2) {
	struct bfs_expr *expr = parse_unary_test(state, eval_flags);
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
			parse_error(state, "${blu}%s${rs} is missing platform support.\n", expr->argv[0]);
		} else {
			parse_error(state, "${blu}%s${rs}: Invalid flags ${bld}%s${rs}.\n", expr->argv[0], flags);
		}
		bfs_expr_free(expr);
		return NULL;
	}

	return expr;
}

/**
 * Parse -fls FILE.
 */
static struct bfs_expr *parse_fls(struct parser_state *state, int arg1, int arg2) {
	struct bfs_expr *expr = parse_unary_action(state, eval_fls);
	if (!expr) {
		goto fail;
	}

	if (expr_open(state, expr, expr->argv[1]) != 0) {
		goto fail;
	}

	expr_set_always_true(expr);
	expr->cost = PRINT_COST;
	expr->reftime = state->now;

	// We'll need these for user/group names, so initialize them now to
	// avoid EMFILE later
	bfs_ctx_users(state->ctx);
	bfs_ctx_groups(state->ctx);

	return expr;

fail:
	bfs_expr_free(expr);
	return NULL;
}

/**
 * Parse -fprint FILE.
 */
static struct bfs_expr *parse_fprint(struct parser_state *state, int arg1, int arg2) {
	struct bfs_expr *expr = parse_unary_action(state, eval_fprint);
	if (expr) {
		expr_set_always_true(expr);
		expr->cost = PRINT_COST;
		if (expr_open(state, expr, expr->argv[1]) != 0) {
			goto fail;
		}
	}
	return expr;

fail:
	bfs_expr_free(expr);
	return NULL;
}

/**
 * Parse -fprint0 FILE.
 */
static struct bfs_expr *parse_fprint0(struct parser_state *state, int arg1, int arg2) {
	struct bfs_expr *expr = parse_unary_action(state, eval_fprint0);
	if (expr) {
		expr_set_always_true(expr);
		expr->cost = PRINT_COST;
		if (expr_open(state, expr, expr->argv[1]) != 0) {
			goto fail;
		}
	}
	return expr;

fail:
	bfs_expr_free(expr);
	return NULL;
}

/**
 * Parse -fprintf FILE FORMAT.
 */
static struct bfs_expr *parse_fprintf(struct parser_state *state, int arg1, int arg2) {
	const char *arg = state->argv[0];

	const char *file = state->argv[1];
	if (!file) {
		parse_error(state, "${blu}%s${rs} needs a file.\n", arg);
		return NULL;
	}

	const char *format = state->argv[2];
	if (!format) {
		parse_error(state, "${blu}%s${rs} needs a format string.\n", arg);
		return NULL;
	}

	struct bfs_expr *expr = parse_action(state, eval_fprintf, 3);
	if (!expr) {
		return NULL;
	}

	expr_set_always_true(expr);

	expr->cost = PRINT_COST;

	if (expr_open(state, expr, file) != 0) {
		goto fail;
	}

	expr->printf = bfs_printf_parse(state->ctx, format);
	if (!expr->printf) {
		goto fail;
	}

	return expr;

fail:
	bfs_expr_free(expr);
	return NULL;
}

/**
 * Parse -fstype TYPE.
 */
static struct bfs_expr *parse_fstype(struct parser_state *state, int arg1, int arg2) {
	if (!bfs_ctx_mtab(state->ctx)) {
		parse_error(state, "Couldn't parse the mount table: %m.\n");
		return NULL;
	}

	struct bfs_expr *expr = parse_unary_test(state, eval_fstype);
	if (expr) {
		expr->cost = STAT_COST;
	}
	return expr;
}

/**
 * Parse -gid/-group.
 */
static struct bfs_expr *parse_group(struct parser_state *state, int arg1, int arg2) {
	const struct bfs_groups *groups = bfs_ctx_groups(state->ctx);
	if (!groups) {
		parse_error(state, "Couldn't parse the group table: %m.\n");
		return NULL;
	}

	const char *arg = state->argv[0];

	struct bfs_expr *expr = parse_unary_test(state, eval_gid);
	if (!expr) {
		return NULL;
	}

	const struct group *grp = bfs_getgrnam(groups, expr->argv[1]);
	if (grp) {
		expr->num = grp->gr_gid;
		expr->int_cmp = BFS_INT_EQUAL;
	} else if (looks_like_icmp(expr->argv[1])) {
		if (!parse_icmp(state, expr->argv[1], expr, 0)) {
			goto fail;
		}
	} else {
		parse_error(state, "${blu}%s${rs} ${bld}%s${rs}: No such group.\n", arg, expr->argv[1]);
		goto fail;
	}

	expr->cost = STAT_COST;

	return expr;

fail:
	bfs_expr_free(expr);
	return NULL;
}

/**
 * Parse -unique.
 */
static struct bfs_expr *parse_unique(struct parser_state *state, int arg1, int arg2) {
	state->ctx->unique = true;
	return parse_nullary_option(state);
}

/**
 * Parse -used N.
 */
static struct bfs_expr *parse_used(struct parser_state *state, int arg1, int arg2) {
	struct bfs_expr *expr = parse_test_icmp(state, eval_used);
	if (expr) {
		expr->cost = STAT_COST;
	}
	return expr;
}

/**
 * Parse -uid/-user.
 */
static struct bfs_expr *parse_user(struct parser_state *state, int arg1, int arg2) {
	const struct bfs_users *users = bfs_ctx_users(state->ctx);
	if (!users) {
		parse_error(state, "Couldn't parse the user table: %m.\n");
		return NULL;
	}

	const char *arg = state->argv[0];

	struct bfs_expr *expr = parse_unary_test(state, eval_uid);
	if (!expr) {
		return NULL;
	}

	const struct passwd *pwd = bfs_getpwnam(users, expr->argv[1]);
	if (pwd) {
		expr->num = pwd->pw_uid;
		expr->int_cmp = BFS_INT_EQUAL;
	} else if (looks_like_icmp(expr->argv[1])) {
		if (!parse_icmp(state, expr->argv[1], expr, 0)) {
			goto fail;
		}
	} else {
		parse_error(state, "${blu}%s${rs} ${bld}%s${rs}: No such user.\n", arg, expr->argv[1]);
		goto fail;
	}

	expr->cost = STAT_COST;

	return expr;

fail:
	bfs_expr_free(expr);
	return NULL;
}

/**
 * Parse -hidden.
 */
static struct bfs_expr *parse_hidden(struct parser_state *state, int arg1, int arg2) {
	struct bfs_expr *expr = parse_nullary_test(state, eval_hidden);
	if (expr) {
		expr->probability = 0.01;
	}
	return expr;
}

/**
 * Parse -(no)?ignore_readdir_race.
 */
static struct bfs_expr *parse_ignore_races(struct parser_state *state, int ignore, int arg2) {
	state->ctx->ignore_races = ignore;
	return parse_nullary_option(state);
}

/**
 * Parse -inum N.
 */
static struct bfs_expr *parse_inum(struct parser_state *state, int arg1, int arg2) {
	struct bfs_expr *expr = parse_test_icmp(state, eval_inum);
	if (expr) {
		expr->cost = STAT_COST;
		expr->probability = expr->int_cmp == BFS_INT_EQUAL ? 0.01 : 0.50;
	}
	return expr;
}

/**
 * Parse -links N.
 */
static struct bfs_expr *parse_links(struct parser_state *state, int arg1, int arg2) {
	struct bfs_expr *expr = parse_test_icmp(state, eval_links);
	if (expr) {
		expr->cost = STAT_COST;
		expr->probability = bfs_expr_cmp(expr, 1) ? 0.99 : 0.01;
	}
	return expr;
}

/**
 * Parse -ls.
 */
static struct bfs_expr *parse_ls(struct parser_state *state, int arg1, int arg2) {
	struct bfs_expr *expr = parse_nullary_action(state, eval_fls);
	if (!expr) {
		return NULL;
	}

	init_print_expr(state, expr);
	expr->reftime = state->now;

	// We'll need these for user/group names, so initialize them now to
	// avoid EMFILE later
	bfs_ctx_users(state->ctx);
	bfs_ctx_groups(state->ctx);

	return expr;
}

/**
 * Parse -mount.
 */
static struct bfs_expr *parse_mount(struct parser_state *state, int arg1, int arg2) {
	parse_warning(state,
	              "In the future, ${blu}%s${rs} will skip mount points entirely, unlike\n"
	              "${blu}-xdev${rs}, due to http://austingroupbugs.net/view.php?id=1133.\n\n",
	              state->argv[0]);

	state->ctx->flags |= BFTW_PRUNE_MOUNTS;
	state->mount_arg = state->argv[0];
	return parse_nullary_option(state);
}

/**
 * Common code for fnmatch() tests.
 */
static struct bfs_expr *parse_fnmatch(const struct parser_state *state, struct bfs_expr *expr, bool casefold) {
	if (!expr) {
		return NULL;
	}

	const char *arg = expr->argv[0];
	const char *pattern = expr->argv[1];

	if (casefold) {
#ifdef FNM_CASEFOLD
		expr->num = FNM_CASEFOLD;
#else
		parse_error(state, "${blu}%s${rs} is missing platform support.\n", arg);
		bfs_expr_free(expr);
		return NULL;
#endif
	} else {
		expr->num = 0;
	}

	// POSIX says, about fnmatch():
	//
	//     If pattern ends with an unescaped <backslash>, fnmatch() shall
	//     return a non-zero value (indicating either no match or an error).
	//
	// But not all implementations obey this, so check for it ourselves.
	size_t i, len = strlen(pattern);
	for (i = 0; i < len; ++i) {
		if (pattern[len - i - 1] != '\\') {
			break;
		}
	}
	if (i % 2 != 0) {
		parse_warning(state, "${blu}%s${rs} ${bld}%s${rs}: Unescaped trailing backslash.\n\n", arg, pattern);
		bfs_expr_free(expr);
		return &bfs_false;
	}

	expr->cost = 400.0;

	if (strchr(pattern, '*')) {
		expr->probability = 0.5;
	} else {
		expr->probability = 0.1;
	}

	return expr;
}

/**
 * Parse -i?name.
 */
static struct bfs_expr *parse_name(struct parser_state *state, int casefold, int arg2) {
	struct bfs_expr *expr = parse_unary_test(state, eval_name);
	return parse_fnmatch(state, expr, casefold);
}

/**
 * Parse -i?path, -i?wholename.
 */
static struct bfs_expr *parse_path(struct parser_state *state, int casefold, int arg2) {
	struct bfs_expr *expr = parse_unary_test(state, eval_path);
	return parse_fnmatch(state, expr, casefold);
}

/**
 * Parse -i?lname.
 */
static struct bfs_expr *parse_lname(struct parser_state *state, int casefold, int arg2) {
	struct bfs_expr *expr = parse_unary_test(state, eval_lname);
	return parse_fnmatch(state, expr, casefold);
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
static int parse_reftime(const struct parser_state *state, struct bfs_expr *expr) {
	if (parse_timestamp(expr->argv[1], &expr->reftime) == 0) {
		return 0;
	} else if (errno != EINVAL) {
		parse_error(state, "${blu}%s${rs} ${bld}%s${rs}: %m.\n", expr->argv[0], expr->argv[1]);
		return -1;
	}

	parse_error(state, "${blu}%s${rs} ${bld}%s${rs}: Invalid timestamp.\n\n", expr->argv[0], expr->argv[1]);
	fprintf(stderr, "Supported timestamp formats are ISO 8601-like, e.g.\n\n");

	struct tm tm;
	if (xlocaltime(&state->now.tv_sec, &tm) != 0) {
		parse_perror(state, "xlocaltime()");
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
	int tz_hour = gmtoff/3600;
	int tz_min = (labs(gmtoff)/60)%60;
	fprintf(stderr, "  - %04d-%02d-%02dT%02d:%02d:%02d%+03d:%02d\n",
	        year, month, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, tz_hour, tz_min);

	if (xgmtime(&state->now.tv_sec, &tm) != 0) {
		parse_perror(state, "xgmtime()");
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
static struct bfs_expr *parse_newerxy(struct parser_state *state, int arg1, int arg2) {
	const char *arg = state->argv[0];
	if (strlen(arg) != 8) {
		parse_error(state, "Expected ${blu}-newer${bld}XY${rs}; found ${blu}-newer${bld}%s${rs}.\n", arg + 6);
		return NULL;
	}

	struct bfs_expr *expr = parse_unary_test(state, eval_newer);
	if (!expr) {
		goto fail;
	}

	expr->stat_field = parse_newerxy_field(arg[6]);
	if (!expr->stat_field) {
		parse_error(state,
		            "${blu}%s${rs}: For ${blu}-newer${bld}XY${rs}, ${bld}X${rs} should be ${bld}a${rs}, ${bld}c${rs}, ${bld}m${rs}, or ${bld}B${rs}, not ${er}%c${rs}.\n",
		            arg, arg[6]);
		goto fail;
	}

	if (arg[7] == 't') {
		if (parse_reftime(state, expr) != 0) {
			goto fail;
		}
	} else {
		enum bfs_stat_field field = parse_newerxy_field(arg[7]);
		if (!field) {
			parse_error(state,
			            "${blu}%s${rs}: For ${blu}-newer${bld}XY${rs}, ${bld}Y${rs} should be ${bld}a${rs}, ${bld}c${rs}, ${bld}m${rs}, ${bld}B${rs}, or ${bld}t${rs}, not ${er}%c${rs}.\n",
			            arg, arg[7]);
			goto fail;
		}

		struct bfs_stat sb;
		if (stat_arg(state, expr, expr->argv[1], &sb) != 0) {
			goto fail;
		}


		const struct timespec *reftime = bfs_stat_time(&sb, field);
		if (!reftime) {
			parse_error(state, "${blu}%s${rs} ${bld}%s${rs}: Couldn't get file %s.\n", arg, expr->argv[1], bfs_stat_field_name(field));
			goto fail;
		}

		expr->reftime = *reftime;
	}

	expr->cost = STAT_COST;

	return expr;

fail:
	bfs_expr_free(expr);
	return NULL;
}

/**
 * Parse -nogroup.
 */
static struct bfs_expr *parse_nogroup(struct parser_state *state, int arg1, int arg2) {
	if (!bfs_ctx_groups(state->ctx)) {
		parse_error(state, "Couldn't parse the group table: %m.\n");
		return NULL;
	}

	struct bfs_expr *expr = parse_nullary_test(state, eval_nogroup);
	if (expr) {
		expr->cost = STAT_COST;
		expr->probability = 0.01;
	}
	return expr;
}

/**
 * Parse -nohidden.
 */
static struct bfs_expr *parse_nohidden(struct parser_state *state, int arg1, int arg2) {
	struct bfs_expr *hidden = bfs_expr_new(eval_hidden, 1, &fake_hidden_arg);
	if (!hidden) {
		return NULL;
	}

	hidden->probability = 0.01;
	hidden->pure = true;

	struct bfs_ctx *ctx = state->ctx;
	ctx->exclude = new_binary_expr(eval_or, ctx->exclude, hidden, &fake_or_arg);
	if (!ctx->exclude) {
		return NULL;
	}

	parser_advance(state, T_OPTION, 1);
	return &bfs_true;
}

/**
 * Parse -noleaf.
 */
static struct bfs_expr *parse_noleaf(struct parser_state *state, int arg1, int arg2) {
	parse_warning(state, "${ex}bfs${rs} does not apply the optimization that ${blu}%s${rs} inhibits.\n\n", state->argv[0]);
	return parse_nullary_option(state);
}

/**
 * Parse -nouser.
 */
static struct bfs_expr *parse_nouser(struct parser_state *state, int arg1, int arg2) {
	if (!bfs_ctx_users(state->ctx)) {
		parse_error(state, "Couldn't parse the user table: %m.\n");
		return NULL;
	}

	struct bfs_expr *expr = parse_nullary_test(state, eval_nouser);
	if (expr) {
		expr->cost = STAT_COST;
		expr->probability = 0.01;
	}
	return expr;
}

/**
 * Parse a permission mode like chmod(1).
 */
static int parse_mode(const struct parser_state *state, const char *mode, struct bfs_expr *expr) {
	if (mode[0] >= '0' && mode[0] <= '9') {
		unsigned int parsed;
		if (!parse_int(state, mode, &parsed, 8 | IF_INT | IF_UNSIGNED | IF_QUIET)) {
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

	// State machine state
	enum {
		MODE_CLAUSE,
		MODE_WHO,
		MODE_ACTION,
		MODE_ACTION_APPLY,
		MODE_OP,
		MODE_PERM,
	} mstate = MODE_CLAUSE;

	enum {
		MODE_PLUS,
		MODE_MINUS,
		MODE_EQUALS,
	} op;

	mode_t who;
	mode_t file_change;
	mode_t dir_change;

	const char *i = mode;
	while (true) {
		switch (mstate) {
		case MODE_CLAUSE:
			who = 0;
			mstate = MODE_WHO;
			BFS_FALLTHROUGH;

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
				mstate = MODE_ACTION;
				continue;
			}
			break;

		case MODE_ACTION_APPLY:
			switch (op) {
			case MODE_EQUALS:
				expr->file_mode &= ~who;
				expr->dir_mode &= ~who;
				BFS_FALLTHROUGH;
			case MODE_PLUS:
				expr->file_mode |= file_change;
				expr->dir_mode |= dir_change;
				break;
			case MODE_MINUS:
				expr->file_mode &= ~file_change;
				expr->dir_mode &= ~dir_change;
				break;
			}
			BFS_FALLTHROUGH;

		case MODE_ACTION:
			if (who == 0) {
				who = 0777;
			}

			switch (*i) {
			case '+':
				op = MODE_PLUS;
				mstate = MODE_OP;
				break;
			case '-':
				op = MODE_MINUS;
				mstate = MODE_OP;
				break;
			case '=':
				op = MODE_EQUALS;
				mstate = MODE_OP;
				break;

			case ',':
				if (mstate == MODE_ACTION_APPLY) {
					mstate = MODE_CLAUSE;
				} else {
					goto fail;
				}
				break;

			case '\0':
				if (mstate == MODE_ACTION_APPLY) {
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
				mstate = MODE_PERM;
				continue;
			}

			file_change |= (file_change << 6) | (file_change << 3);
			file_change &= who;
			dir_change |= (dir_change << 6) | (dir_change << 3);
			dir_change &= who;
			mstate = MODE_ACTION_APPLY;
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
				BFS_FALLTHROUGH;
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
				mstate = MODE_ACTION_APPLY;
				continue;
			}
			break;
		}

		++i;
	}

done:
	return 0;

fail:
	parse_error(state, "${blu}%s${rs} ${bld}%s${rs}: Invalid mode.\n", expr->argv[0], mode);
	return -1;
}

/**
 * Parse -perm MODE.
 */
static struct bfs_expr *parse_perm(struct parser_state *state, int field, int arg2) {
	struct bfs_expr *expr = parse_unary_test(state, eval_perm);
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
		BFS_FALLTHROUGH;
	default:
		expr->mode_cmp = BFS_MODE_EQUAL;
		break;
	}

	if (parse_mode(state, mode, expr) != 0) {
		goto fail;
	}

	expr->cost = STAT_COST;

	return expr;

fail:
	bfs_expr_free(expr);
	return NULL;
}

/**
 * Parse -print.
 */
static struct bfs_expr *parse_print(struct parser_state *state, int arg1, int arg2) {
	struct bfs_expr *expr = parse_nullary_action(state, eval_fprint);
	if (expr) {
		init_print_expr(state, expr);
	}
	return expr;
}

/**
 * Parse -print0.
 */
static struct bfs_expr *parse_print0(struct parser_state *state, int arg1, int arg2) {
	struct bfs_expr *expr = parse_nullary_action(state, eval_fprint0);
	if (expr) {
		init_print_expr(state, expr);
	}
	return expr;
}

/**
 * Parse -printf FORMAT.
 */
static struct bfs_expr *parse_printf(struct parser_state *state, int arg1, int arg2) {
	struct bfs_expr *expr = parse_unary_action(state, eval_fprintf);
	if (!expr) {
		return NULL;
	}

	init_print_expr(state, expr);

	expr->printf = bfs_printf_parse(state->ctx, expr->argv[1]);
	if (!expr->printf) {
		bfs_expr_free(expr);
		return NULL;
	}

	return expr;
}

/**
 * Parse -printx.
 */
static struct bfs_expr *parse_printx(struct parser_state *state, int arg1, int arg2) {
	struct bfs_expr *expr = parse_nullary_action(state, eval_fprintx);
	if (expr) {
		init_print_expr(state, expr);
	}
	return expr;
}

/**
 * Parse -prune.
 */
static struct bfs_expr *parse_prune(struct parser_state *state, int arg1, int arg2) {
	state->prune_arg = state->argv[0];

	struct bfs_expr *expr = parse_nullary_action(state, eval_prune);
	if (expr) {
		expr_set_always_true(expr);
	}
	return expr;
}

/**
 * Parse -quit.
 */
static struct bfs_expr *parse_quit(struct parser_state *state, int arg1, int arg2) {
	struct bfs_expr *expr = parse_nullary_action(state, eval_quit);
	if (expr) {
		expr_set_never_returns(expr);
	}
	return expr;
}

/**
 * Parse -i?regex.
 */
static struct bfs_expr *parse_regex(struct parser_state *state, int flags, int arg2) {
	struct bfs_expr *expr = parse_unary_test(state, eval_regex);
	if (!expr) {
		goto fail;
	}

	if (bfs_regcomp(&expr->regex, expr->argv[1], state->regex_type, flags) != 0) {
		if (!expr->regex) {
			parse_perror(state, "bfs_regcomp()");
			goto fail;
		}

		char *str = bfs_regerror(expr->regex);
		if (!str) {
			parse_perror(state, "bfs_regerror()");
			goto fail;
		}

		parse_error(state, "${blu}%s${rs} ${bld}%s${rs}: %s.\n", expr->argv[0], expr->argv[1], str);
		free(str);
		goto fail;
	}

	return expr;

fail:
	bfs_expr_free(expr);
	return NULL;
}

/**
 * Parse -E.
 */
static struct bfs_expr *parse_regex_extended(struct parser_state *state, int arg1, int arg2) {
	state->regex_type = BFS_REGEX_POSIX_EXTENDED;
	return parse_nullary_flag(state);
}

/**
 * Parse -regextype TYPE.
 */
static struct bfs_expr *parse_regextype(struct parser_state *state, int arg1, int arg2) {
	struct bfs_ctx *ctx = state->ctx;
	CFILE *cfile = ctx->cerr;

	const char *arg = state->argv[0];
	const char *type = state->argv[1];
	if (!type) {
		parse_error(state, "${blu}%s${rs} needs a value.\n\n", arg);
		goto list_types;
	}

	// See https://www.gnu.org/software/gnulib/manual/html_node/Predefined-Syntaxes.html
	if (strcmp(type, "posix-basic") == 0
	    || strcmp(type, "ed") == 0
	    || strcmp(type, "sed") == 0) {
		state->regex_type = BFS_REGEX_POSIX_BASIC;
	} else if (strcmp(type, "posix-extended") == 0) {
		state->regex_type = BFS_REGEX_POSIX_EXTENDED;
#if BFS_WITH_ONIGURUMA
	} else if (strcmp(type, "emacs") == 0) {
		state->regex_type = BFS_REGEX_EMACS;
	} else if (strcmp(type, "grep") == 0) {
		state->regex_type = BFS_REGEX_GREP;
#endif
	} else if (strcmp(type, "help") == 0) {
		state->just_info = true;
		cfile = ctx->cout;
		goto list_types;
	} else {
		parse_error(state, "${blu}%s${rs} ${bld}%s${rs}: Unsupported regex type.\n\n", arg, type);
		goto list_types;
	}

	return parse_unary_positional_option(state);

list_types:
	cfprintf(cfile, "Supported types are:\n\n");
	cfprintf(cfile, "  ${bld}posix-basic${rs}:    POSIX basic regular expressions (BRE)\n");
	cfprintf(cfile, "  ${bld}posix-extended${rs}: POSIX extended regular expressions (ERE)\n");
	cfprintf(cfile, "  ${bld}ed${rs}:             Like ${grn}ed${rs} (same as ${bld}posix-basic${rs})\n");
#if BFS_WITH_ONIGURUMA
	cfprintf(cfile, "  ${bld}emacs${rs}:          Like ${grn}emacs${rs}\n");
	cfprintf(cfile, "  ${bld}grep${rs}:           Like ${grn}grep${rs}\n");
#endif
	cfprintf(cfile, "  ${bld}sed${rs}:            Like ${grn}sed${rs} (same as ${bld}posix-basic${rs})\n");
	return NULL;
}

/**
 * Parse -s.
 */
static struct bfs_expr *parse_s(struct parser_state *state, int arg1, int arg2) {
	state->ctx->flags |= BFTW_SORT;
	return parse_nullary_flag(state);
}

/**
 * Parse -samefile FILE.
 */
static struct bfs_expr *parse_samefile(struct parser_state *state, int arg1, int arg2) {
	struct bfs_expr *expr = parse_unary_test(state, eval_samefile);
	if (!expr) {
		return NULL;
	}

	struct bfs_stat sb;
	if (stat_arg(state, expr, expr->argv[1], &sb) != 0) {
		bfs_expr_free(expr);
		return NULL;
	}

	expr->dev = sb.dev;
	expr->ino = sb.ino;

	expr->cost = STAT_COST;
	expr->probability = 0.01;

	return expr;
}

/**
 * Parse -S STRATEGY.
 */
static struct bfs_expr *parse_search_strategy(struct parser_state *state, int arg1, int arg2) {
	struct bfs_ctx *ctx = state->ctx;
	CFILE *cfile = ctx->cerr;

	const char *flag = state->argv[0];
	const char *arg = state->argv[1];
	if (!arg) {
		parse_error(state, "${cyn}%s${rs} needs an argument.\n\n", flag);
		goto list_strategies;
	}


	if (strcmp(arg, "bfs") == 0) {
		ctx->strategy = BFTW_BFS;
	} else if (strcmp(arg, "dfs") == 0) {
		ctx->strategy = BFTW_DFS;
	} else if (strcmp(arg, "ids") == 0) {
		ctx->strategy = BFTW_IDS;
	} else if (strcmp(arg, "eds") == 0) {
		ctx->strategy = BFTW_EDS;
	} else if (strcmp(arg, "help") == 0) {
		state->just_info = true;
		cfile = ctx->cout;
		goto list_strategies;
	} else {
		parse_error(state, "${cyn}%s${rs} ${bld}%s${rs}: Unrecognized search strategy.\n\n", flag, arg);
		goto list_strategies;
	}

	return parse_unary_flag(state);

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
static struct bfs_expr *parse_since(struct parser_state *state, int field, int arg2) {
	struct bfs_expr *expr = parse_unary_test(state, eval_newer);
	if (!expr) {
		return NULL;
	}

	if (parse_reftime(state, expr) != 0) {
		goto fail;
	}

	expr->cost = STAT_COST;
	expr->stat_field = field;
	return expr;

fail:
	bfs_expr_free(expr);
	return NULL;
}

/**
 * Parse -size N[cwbkMGTP]?.
 */
static struct bfs_expr *parse_size(struct parser_state *state, int arg1, int arg2) {
	struct bfs_expr *expr = parse_unary_test(state, eval_size);
	if (!expr) {
		return NULL;
	}

	const char *unit = parse_icmp(state, expr->argv[1], expr, IF_PARTIAL_OK);
	if (!unit) {
		goto fail;
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
		goto bad_unit;
	}

	expr->cost = STAT_COST;
	expr->probability = expr->int_cmp == BFS_INT_EQUAL ? 0.01 : 0.50;

	return expr;

bad_unit:
	parse_error(state, "${blu}%s${rs} ${bld}%s${rs}: Expected a size unit (one of ${bld}cwbkMGTP${rs}); found ${er}%s${rs}.\n",
	            expr->argv[0], expr->argv[1], unit);
fail:
	bfs_expr_free(expr);
	return NULL;
}

/**
 * Parse -sparse.
 */
static struct bfs_expr *parse_sparse(struct parser_state *state, int arg1, int arg2) {
	struct bfs_expr *expr = parse_nullary_test(state, eval_sparse);
	if (expr) {
		expr->cost = STAT_COST;
	}
	return expr;
}

/**
 * Parse -status.
 */
static struct bfs_expr *parse_status(struct parser_state *state, int arg1, int arg2) {
	state->ctx->status = true;
	return parse_nullary_option(state);
}

/**
 * Parse -x?type [bcdpflsD].
 */
static struct bfs_expr *parse_type(struct parser_state *state, int x, int arg2) {
	bfs_eval_fn *eval = x ? eval_xtype : eval_type;
	struct bfs_expr *expr = parse_unary_test(state, eval);
	if (!expr) {
		return NULL;
	}

	unsigned int types = 0;
	double probability = 0.0;

	const char *c = expr->argv[1];
	while (true) {
		enum bfs_type type;
		double type_prob;

		switch (*c) {
		case 'b':
			type = BFS_BLK;
			type_prob = 0.00000721183;
			break;
		case 'c':
			type = BFS_CHR;
			type_prob = 0.0000499855;
			break;
		case 'd':
			type = BFS_DIR;
			type_prob = 0.114475;
			break;
		case 'D':
			type = BFS_DOOR;
			type_prob = 0.000001;
			break;
		case 'p':
			type = BFS_FIFO;
			type_prob = 0.00000248684;
			break;
		case 'f':
			type = BFS_REG;
			type_prob = 0.859772;
			break;
		case 'l':
			type = BFS_LNK;
			type_prob = 0.0256816;
			break;
		case 's':
			type = BFS_SOCK;
			type_prob = 0.0000116881;
			break;
		case 'w':
			type = BFS_WHT;
			type_prob = 0.000001;
			break;

		case '\0':
			parse_error(state, "${blu}%s${rs} ${bld}%s${rs}: Expected a type flag.\n", expr->argv[0], expr->argv[1]);
			goto fail;

		default:
			parse_error(state, "${blu}%s${rs} ${bld}%s${rs}: Unknown type flag ${er}%c${rs} (expected one of [${bld}bcdpflsD${rs}]).\n",
			            expr->argv[0], expr->argv[1], *c);
			goto fail;
		}

		unsigned int flag = 1 << type;
		if (!(types & flag)) {
			types |= flag;
			probability += type_prob;
		}

		++c;
		if (*c == '\0') {
			break;
		} else if (*c == ',') {
			++c;
			continue;
		} else {
			parse_error(state, "${blu}%s${rs} ${bld}%s${rs}: Types must be comma-separated.\n", expr->argv[0], expr->argv[1]);
			goto fail;
		}
	}

	expr->num = types;
	expr->probability = probability;

	if (x && state->ctx->optlevel < 4) {
		// Since -xtype dereferences symbolic links, it may have side
		// effects such as reporting permission errors, and thus
		// shouldn't be re-ordered without aggressive optimizations
		expr->pure = false;
	}

	return expr;

fail:
	bfs_expr_free(expr);
	return NULL;
}

/**
 * Parse -(no)?warn.
 */
static struct bfs_expr *parse_warn(struct parser_state *state, int warn, int arg2) {
	state->ctx->warn = warn;
	return parse_nullary_positional_option(state);
}

/**
 * Parse -xattr.
 */
static struct bfs_expr *parse_xattr(struct parser_state *state, int arg1, int arg2) {
#if BFS_CAN_CHECK_XATTRS
	struct bfs_expr *expr = parse_nullary_test(state, eval_xattr);
	if (expr) {
		expr->cost = STAT_COST;
		expr->probability = 0.01;
	}
	return expr;
#else
	parse_error(state, "${blu}%s${rs} is missing platform support.\n", state->argv[0]);
	return NULL;
#endif
}

/**
 * Parse -xattrname.
 */
static struct bfs_expr *parse_xattrname(struct parser_state *state, int arg1, int arg2) {
#if BFS_CAN_CHECK_XATTRS
	struct bfs_expr *expr = parse_unary_test(state, eval_xattrname);
	if (expr) {
		expr->cost = STAT_COST;
		expr->probability = 0.01;
	}
	return expr;
#else
	parse_error(state, "${blu}%s${rs} is missing platform support.\n", state->argv[0]);
	return NULL;
#endif
}

/**
 * Parse -xdev.
 */
static struct bfs_expr *parse_xdev(struct parser_state *state, int arg1, int arg2) {
	state->ctx->flags |= BFTW_PRUNE_MOUNTS;
	state->xdev_arg = state->argv[0];
	return parse_nullary_option(state);
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

	if (strcmp(xbasename(exe), "less") == 0) {
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
static struct bfs_expr *parse_help(struct parser_state *state, int arg1, int arg2) {
	CFILE *cout = state->ctx->cout;

	pid_t pager = -1;
	if (state->stdout_tty) {
		cout = launch_pager(&pager, cout);
	}

	cfprintf(cout, "Usage: ${ex}%s${rs} [${cyn}flags${rs}...] [${mag}paths${rs}...] [${blu}expression${rs}...]\n\n",
		 state->command);

	cfprintf(cout, "${ex}bfs${rs} is compatible with ${ex}find${rs}, with some extensions. "
		       "${cyn}Flags${rs} (${cyn}-H${rs}/${cyn}-L${rs}/${cyn}-P${rs} etc.), ${mag}paths${rs},\n"
		       "and ${blu}expressions${rs} may be freely mixed in any order.\n\n");

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
	cfprintf(cout, "      (default: ${cyn}-S${rs} ${bld}bfs${rs})\n\n");

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
	cfprintf(cout, "      Whether to report an error if ${ex}bfs${rs} detects that the file tree is modified\n");
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
		waitpid(pager, NULL, 0);
	}

	state->just_info = true;
	return NULL;
}

/**
 * "Parse" -version.
 */
static struct bfs_expr *parse_version(struct parser_state *state, int arg1, int arg2) {
	cfprintf(state->ctx->cout, "${ex}bfs${rs} ${bld}%s${rs}\n\n", BFS_VERSION);

	printf("%s\n", BFS_HOMEPAGE);

	state->just_info = true;
	return NULL;
}

typedef struct bfs_expr *parse_fn(struct parser_state *state, int arg1, int arg2);

/**
 * An entry in the parse table for literals.
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
 * The parse table for literals.
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
	int best_dist;

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
 * LITERAL : OPTION
 *         | TEST
 *         | ACTION
 */
static struct bfs_expr *parse_literal(struct parser_state *state) {
	// Paths are already skipped at this point
	const char *arg = state->argv[0];

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

	CFILE *cerr = state->ctx->cerr;
	parse_error(state, "Unknown argument ${er}%s${rs}; did you mean ", arg);
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

	if (!state->interactive || !match->parse) {
		fprintf(stderr, "\n");
		goto unmatched;
	}

	fprintf(stderr, " ");
	if (ynprompt() <= 0) {
		goto unmatched;
	}

	fprintf(stderr, "\n");
	state->argv[0] = match->arg;

matched:
	return match->parse(state, match->arg1, match->arg2);

unmatched:
	return NULL;

unexpected:
	parse_error(state, "Expected a predicate; found ${er}%s${rs}.\n", arg);
	return NULL;
}

/**
 * FACTOR : "(" EXPR ")"
 *        | "!" FACTOR | "-not" FACTOR
 *        | "-exclude" FACTOR
 *        | LITERAL
 */
static struct bfs_expr *parse_factor(struct parser_state *state) {
	if (skip_paths(state) != 0) {
		return NULL;
	}

	const char *arg = state->argv[0];
	if (!arg) {
		parse_error(state, "Expression terminated prematurely after ${red}%s${rs}.\n", state->last_arg);
		return NULL;
	}

	if (strcmp(arg, "(") == 0) {
		parser_advance(state, T_OPERATOR, 1);

		struct bfs_expr *expr = parse_expr(state);
		if (!expr) {
			return NULL;
		}

		if (skip_paths(state) != 0) {
			bfs_expr_free(expr);
			return NULL;
		}

		arg = state->argv[0];
		if (!arg || strcmp(arg, ")") != 0) {
			parse_error(state, "Expected a ${red})${rs} after ${blu}%s${rs}.\n", state->argv[-1]);
			bfs_expr_free(expr);
			return NULL;
		}
		parser_advance(state, T_OPERATOR, 1);

		return expr;
	} else if (strcmp(arg, "-exclude") == 0) {
		parser_advance(state, T_OPERATOR, 1);

		if (state->excluding) {
			parse_error(state, "${er}%s${rs} is not supported within ${red}-exclude${rs}.\n", arg);
			return NULL;
		}
		state->excluding = true;

		struct bfs_expr *factor = parse_factor(state);
		if (!factor) {
			return NULL;
		}

		state->excluding = false;

		struct bfs_ctx *ctx = state->ctx;
		ctx->exclude = new_binary_expr(eval_or, ctx->exclude, factor, &fake_or_arg);
		if (!ctx->exclude) {
			return NULL;
		}

		return &bfs_true;
	} else if (strcmp(arg, "!") == 0 || strcmp(arg, "-not") == 0) {
		char **argv = parser_advance(state, T_OPERATOR, 1);

		struct bfs_expr *factor = parse_factor(state);
		if (!factor) {
			return NULL;
		}

		return new_unary_expr(eval_not, factor, argv);
	} else {
		return parse_literal(state);
	}
}

/**
 * TERM : FACTOR
 *      | TERM FACTOR
 *      | TERM "-a" FACTOR
 *      | TERM "-and" FACTOR
 */
static struct bfs_expr *parse_term(struct parser_state *state) {
	struct bfs_expr *term = parse_factor(state);

	while (term) {
		if (skip_paths(state) != 0) {
			bfs_expr_free(term);
			return NULL;
		}

		const char *arg = state->argv[0];
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
			argv = parser_advance(state, T_OPERATOR, 1);
		}

		struct bfs_expr *lhs = term;
		struct bfs_expr *rhs = parse_factor(state);
		if (!rhs) {
			bfs_expr_free(lhs);
			return NULL;
		}

		term = new_binary_expr(eval_and, lhs, rhs, argv);
	}

	return term;
}

/**
 * CLAUSE : TERM
 *        | CLAUSE "-o" TERM
 *        | CLAUSE "-or" TERM
 */
static struct bfs_expr *parse_clause(struct parser_state *state) {
	struct bfs_expr *clause = parse_term(state);

	while (clause) {
		if (skip_paths(state) != 0) {
			bfs_expr_free(clause);
			return NULL;
		}

		const char *arg = state->argv[0];
		if (!arg) {
			break;
		}

		if (strcmp(arg, "-o") != 0 && strcmp(arg, "-or") != 0) {
			break;
		}

		char **argv = parser_advance(state, T_OPERATOR, 1);

		struct bfs_expr *lhs = clause;
		struct bfs_expr *rhs = parse_term(state);
		if (!rhs) {
			bfs_expr_free(lhs);
			return NULL;
		}

		clause = new_binary_expr(eval_or, lhs, rhs, argv);
	}

	return clause;
}

/**
 * EXPR : CLAUSE
 *      | EXPR "," CLAUSE
 */
static struct bfs_expr *parse_expr(struct parser_state *state) {
	struct bfs_expr *expr = parse_clause(state);

	while (expr) {
		if (skip_paths(state) != 0) {
			bfs_expr_free(expr);
			return NULL;
		}

		const char *arg = state->argv[0];
		if (!arg) {
			break;
		}

		if (strcmp(arg, ",") != 0) {
			break;
		}

		char **argv = parser_advance(state, T_OPERATOR, 1);

		struct bfs_expr *lhs = expr;
		struct bfs_expr *rhs = parse_clause(state);
		if (!rhs) {
			bfs_expr_free(lhs);
			return NULL;
		}

		expr = new_binary_expr(eval_comma, lhs, rhs, argv);
	}

	return expr;
}

/**
 * Parse the top-level expression.
 */
static struct bfs_expr *parse_whole_expr(struct parser_state *state) {
	if (skip_paths(state) != 0) {
		return NULL;
	}

	struct bfs_expr *expr = &bfs_true;
	if (state->argv[0]) {
		expr = parse_expr(state);
		if (!expr) {
			return NULL;
		}
	}

	if (state->argv[0]) {
		parse_error(state, "Unexpected argument ${er}%s${rs}.\n", state->argv[0]);
		goto fail;
	}

	if (state->implicit_print) {
		struct bfs_expr *print = bfs_expr_new(eval_fprint, 1, &fake_print_arg);
		if (!print) {
			goto fail;
		}
		init_print_expr(state, print);

		expr = new_binary_expr(eval_and, expr, print, &fake_and_arg);
		if (!expr) {
			goto fail;
		}
	}

	if (state->mount_arg && state->xdev_arg) {
		parse_warning(state, "${blu}%s${rs} is redundant in the presence of ${blu}%s${rs}.\n\n", state->xdev_arg, state->mount_arg);
	}

	if (state->ctx->warn && state->depth_arg && state->prune_arg) {
		parse_warning(state, "${blu}%s${rs} does not work in the presence of ${blu}%s${rs}.\n", state->prune_arg, state->depth_arg);

		if (state->interactive) {
			fprintf(stderr, "Do you want to continue? ");
			if (ynprompt() == 0) {
				goto fail;
			}
		}

		fprintf(stderr, "\n");
	}

	if (state->ok_arg && state->stdin_consumed) {
		parse_error(state, "${blu}%s${rs} conflicts with ${blu}-files0-from${rs} ${bld}-${rs}.\n", state->ok_arg);
		goto fail;
	}

	return expr;

fail:
	bfs_expr_free(expr);
	return NULL;
}

void bfs_ctx_dump(const struct bfs_ctx *ctx, enum debug_flags flag) {
	if (!bfs_debug_prefix(ctx, flag)) {
		return;
	}

	CFILE *cerr = ctx->cerr;

	cfprintf(cerr, "${ex}%s${rs} ", ctx->argv[0]);

	if (ctx->flags & BFTW_FOLLOW_ALL) {
		cfprintf(cerr, "${cyn}-L${rs} ");
	} else if (ctx->flags & BFTW_FOLLOW_ROOTS) {
		cfprintf(cerr, "${cyn}-H${rs} ");
	} else {
		cfprintf(cerr, "${cyn}-P${rs} ");
	}

	if (ctx->xargs_safe) {
		cfprintf(cerr, "${cyn}-X${rs} ");
	}

	if (ctx->flags & BFTW_SORT) {
		cfprintf(cerr, "${cyn}-s${rs} ");
	}

	if (ctx->optlevel != 3) {
		cfprintf(cerr, "${cyn}-O${bld}%d${rs} ", ctx->optlevel);
	}

	const char *strategy = NULL;
	switch (ctx->strategy) {
	case BFTW_BFS:
		strategy = "bfs";
		break;
	case BFTW_DFS:
		strategy = "dfs";
		break;
	case BFTW_IDS:
		strategy = "ids";
		break;
	case BFTW_EDS:
		strategy = "eds";
		break;
	}
	assert(strategy);
	cfprintf(cerr, "${cyn}-S${rs} ${bld}%s${rs} ", strategy);

	enum debug_flags debug = ctx->debug;
	if (debug == DEBUG_ALL) {
		cfprintf(cerr, "${cyn}-D${rs} ${bld}all${rs} ");
	} else if (debug) {
		cfprintf(cerr, "${cyn}-D${rs} ");
		for (enum debug_flags i = 1; DEBUG_ALL & i; i <<= 1) {
			if (debug & i) {
				cfprintf(cerr, "${bld}%s${rs}", debug_flag_name(i));
				debug ^= i;
				if (debug) {
					cfprintf(cerr, ",");
				}
			}
		}
		cfprintf(cerr, " ");
	}

	for (size_t i = 0; i < darray_length(ctx->paths); ++i) {
		const char *path = ctx->paths[i];
		char c = path[0];
		if (c == '-' || c == '(' || c == ')' || c == '!' || c == ',') {
			cfprintf(cerr, "${cyn}-f${rs} ");
		}
		cfprintf(cerr, "${mag}%s${rs} ", path);
	}

	if (ctx->cout->colors) {
		cfprintf(cerr, "${blu}-color${rs} ");
	} else {
		cfprintf(cerr, "${blu}-nocolor${rs} ");
	}
	if (ctx->flags & BFTW_POST_ORDER) {
		cfprintf(cerr, "${blu}-depth${rs} ");
	}
	if (ctx->ignore_races) {
		cfprintf(cerr, "${blu}-ignore_readdir_race${rs} ");
	}
	if (ctx->mindepth != 0) {
		cfprintf(cerr, "${blu}-mindepth${rs} ${bld}%d${rs} ", ctx->mindepth);
	}
	if (ctx->maxdepth != INT_MAX) {
		cfprintf(cerr, "${blu}-maxdepth${rs} ${bld}%d${rs} ", ctx->maxdepth);
	}
	if (ctx->flags & BFTW_SKIP_MOUNTS) {
		cfprintf(cerr, "${blu}-mount${rs} ");
	}
	if (ctx->status) {
		cfprintf(cerr, "${blu}-status${rs} ");
	}
	if (ctx->unique) {
		cfprintf(cerr, "${blu}-unique${rs} ");
	}
	if ((ctx->flags & (BFTW_SKIP_MOUNTS | BFTW_PRUNE_MOUNTS)) == BFTW_PRUNE_MOUNTS) {
		cfprintf(cerr, "${blu}-xdev${rs} ");
	}

	if (flag == DEBUG_RATES) {
		if (ctx->exclude != &bfs_false) {
			cfprintf(cerr, "(${red}-exclude${rs} %pE) ", ctx->exclude);
		}
		cfprintf(cerr, "%pE", ctx->expr);
	} else {
		if (ctx->exclude != &bfs_false) {
			cfprintf(cerr, "(${red}-exclude${rs} %pe) ", ctx->exclude);
		}
		cfprintf(cerr, "%pe", ctx->expr);
	}

	fputs("\n", stderr);
}

/**
 * Dump the estimated costs.
 */
static void dump_costs(const struct bfs_ctx *ctx) {
	const struct bfs_expr *expr = ctx->expr;
	bfs_debug(ctx, DEBUG_COST, "       Cost: ~${ylw}%g${rs}\n", expr->cost);
	bfs_debug(ctx, DEBUG_COST, "Probability: ~${ylw}%g%%${rs}\n", 100.0*expr->probability);
}

/**
 * Get the current time.
 */
static int parse_gettime(const struct bfs_ctx *ctx, struct timespec *ts) {
#if _POSIX_TIMERS > 0
	int ret = clock_gettime(CLOCK_REALTIME, ts);
	if (ret != 0) {
		bfs_perror(ctx, "clock_gettime()");
	}
	return ret;
#else
	struct timeval tv;
	int ret = gettimeofday(&tv, NULL);
	if (ret == 0) {
		ts->tv_sec = tv.tv_sec;
		ts->tv_nsec = tv.tv_usec * 1000L;
	} else {
		bfs_perror(ctx, "gettimeofday()");
	}
	return ret;
#endif
}

struct bfs_ctx *bfs_parse_cmdline(int argc, char *argv[]) {
	struct bfs_ctx *ctx = bfs_ctx_new();
	if (!ctx) {
		perror("bfs_new_ctx()");
		goto fail;
	}

	static char* default_argv[] = {"bfs", NULL};
	if (argc < 1) {
		argc = 1;
		argv = default_argv;
	}

	ctx->argv = malloc((argc + 1)*sizeof(*ctx->argv));
	if (!ctx->argv) {
		perror("malloc()");
		goto fail;
	}
	for (int i = 0; i <= argc; ++i) {
		ctx->argv[i] = argv[i];
	}

	enum use_color use_color = COLOR_AUTO;
	if (getenv("NO_COLOR")) {
		// https://no-color.org/
		use_color = COLOR_NEVER;
	}

	ctx->colors = parse_colors(getenv("LS_COLORS"));
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

	struct parser_state state = {
		.ctx = ctx,
		.argv = ctx->argv + 1,
		.command = ctx->argv[0],
		.regex_type = BFS_REGEX_POSIX_BASIC,
		.stdout_tty = stdout_tty,
		.interactive = stdin_tty && stderr_tty,
		.stdin_consumed = false,
		.use_color = use_color,
		.implicit_print = true,
		.implicit_root = true,
		.non_option_seen = false,
		.just_info = false,
		.excluding = false,
		.last_arg = NULL,
		.depth_arg = NULL,
		.prune_arg = NULL,
		.mount_arg = NULL,
		.xdev_arg = NULL,
		.ok_arg = NULL,
	};

	if (strcmp(xbasename(state.command), "find") == 0) {
		// Operate depth-first when invoked as "find"
		ctx->strategy = BFTW_DFS;
	}

	if (parse_gettime(ctx, &state.now) != 0) {
		goto fail;
	}

	ctx->exclude = &bfs_false;
	ctx->expr = parse_whole_expr(&state);
	if (!ctx->expr) {
		if (state.just_info) {
			goto done;
		} else {
			goto fail;
		}
	}

	if (bfs_optimize(ctx) != 0) {
		goto fail;
	}

	if (darray_length(ctx->paths) == 0) {
		if (!state.implicit_root) {
			parse_error(&state, "No root paths specified.\n");
			goto fail;
		} else if (parse_root(&state, ".") != 0) {
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
