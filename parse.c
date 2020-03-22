/****************************************************************************
 * bfs                                                                      *
 * Copyright (C) 2015-2019 Tavian Barnes <tavianator@tavianator.com>        *
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

#include "bfs.h"
#include "cmdline.h"
#include "darray.h"
#include "diag.h"
#include "dstring.h"
#include "eval.h"
#include "exec.h"
#include "expr.h"
#include "fsade.h"
#include "mtab.h"
#include "passwd.h"
#include "printf.h"
#include "spawn.h"
#include "stat.h"
#include "time.h"
#include "typo.h"
#include "util.h"
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <grp.h>
#include <limits.h>
#include <pwd.h>
#include <regex.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

// Strings printed by -D tree for "fake" expressions
static char *fake_and_arg = "-a";
static char *fake_false_arg = "-false";
static char *fake_print_arg = "-print";
static char *fake_true_arg = "-true";

// Cost estimation constants
#define FAST_COST     40.0
#define STAT_COST   1000.0
#define PRINT_COST 20000.0

struct expr expr_true = {
	.eval = eval_true,
	.lhs = NULL,
	.rhs = NULL,
	.pure = true,
	.always_true = true,
	.cost = FAST_COST,
	.probability = 1.0,
	.argc = 1,
	.argv = &fake_true_arg,
};

struct expr expr_false = {
	.eval = eval_false,
	.lhs = NULL,
	.rhs = NULL,
	.pure = true,
	.always_false = true,
	.cost = FAST_COST,
	.probability = 0.0,
	.argc = 1,
	.argv = &fake_false_arg,
};

/**
 * Free an expression.
 */
void free_expr(struct expr *expr) {
	if (!expr || expr == &expr_true || expr == &expr_false) {
		return;
	}

	if (expr->regex) {
		regfree(expr->regex);
		free(expr->regex);
	}

	free_bfs_printf(expr->printf);
	free_bfs_exec(expr->execbuf);

	free_expr(expr->lhs);
	free_expr(expr->rhs);

	free(expr);
}

struct expr *new_expr(eval_fn *eval, size_t argc, char **argv) {
	struct expr *expr = malloc(sizeof(*expr));
	if (!expr) {
		perror("malloc()");
		return NULL;
	}

	expr->eval = eval;
	expr->lhs = NULL;
	expr->rhs = NULL;
	expr->pure = false;
	expr->always_true = false;
	expr->always_false = false;
	expr->cost = FAST_COST;
	expr->probability = 0.5;
	expr->evaluations = 0;
	expr->successes = 0;
	expr->elapsed.tv_sec = 0;
	expr->elapsed.tv_nsec = 0;
	expr->argc = argc;
	expr->argv = argv;
	expr->cfile = NULL;
	expr->regex = NULL;
	expr->execbuf = NULL;
	expr->printf = NULL;
	expr->persistent_fds = 0;
	expr->ephemeral_fds = 0;
	return expr;
}

/**
 * Create a new unary expression.
 */
static struct expr *new_unary_expr(eval_fn *eval, struct expr *rhs, char **argv) {
	struct expr *expr = new_expr(eval, 1, argv);
	if (!expr) {
		free_expr(rhs);
		return NULL;
	}

	expr->rhs = rhs;
	expr->persistent_fds = rhs->persistent_fds;
	expr->ephemeral_fds = rhs->ephemeral_fds;
	return expr;
}

/**
 * Create a new binary expression.
 */
static struct expr *new_binary_expr(eval_fn *eval, struct expr *lhs, struct expr *rhs, char **argv) {
	struct expr *expr = new_expr(eval, 1, argv);
	if (!expr) {
		free_expr(rhs);
		free_expr(lhs);
		return NULL;
	}

	expr->lhs = lhs;
	expr->rhs = rhs;
	expr->persistent_fds = lhs->persistent_fds + rhs->persistent_fds;
	if (lhs->ephemeral_fds > rhs->ephemeral_fds) {
		expr->ephemeral_fds = lhs->ephemeral_fds;
	} else {
		expr->ephemeral_fds = rhs->ephemeral_fds;
	}
	return expr;
}

/**
 * Check if an expression never returns.
 */
bool expr_never_returns(const struct expr *expr) {
	// Expressions that never return are vacuously both always true and always false
	return expr->always_true && expr->always_false;
}

/**
 * Set an expression to always return true.
 */
static void expr_set_always_true(struct expr *expr) {
	expr->always_true = true;
	expr->probability = 1.0;
}

/**
 * Set an expression to never return.
 */
static void expr_set_never_returns(struct expr *expr) {
	expr->always_true = expr->always_false = true;
}

/**
 * Dump the parsed expression tree, for debugging.
 */
void dump_expr(CFILE *cfile, const struct expr *expr, bool verbose) {
	fputs("(", cfile->file);

	if (expr->lhs || expr->rhs) {
		cfprintf(cfile, "${red}%s${rs}", expr->argv[0]);
	} else {
		cfprintf(cfile, "${blu}%s${rs}", expr->argv[0]);
	}

	for (size_t i = 1; i < expr->argc; ++i) {
		cfprintf(cfile, " ${bld}%s${rs}", expr->argv[i]);
	}

	if (verbose) {
		double rate = 0.0, time = 0.0;
		if (expr->evaluations) {
			rate = 100.0*expr->successes/expr->evaluations;
			time = (1.0e9*expr->elapsed.tv_sec + expr->elapsed.tv_nsec)/expr->evaluations;
		}
		cfprintf(cfile, " [${ylw}%zu${rs}/${ylw}%zu${rs}=${ylw}%g%%${rs}; ${ylw}%gns${rs}]", expr->successes, expr->evaluations, rate, time);
	}

	if (expr->lhs) {
		fputs(" ", cfile->file);
		dump_expr(cfile, expr->lhs, verbose);
	}

	if (expr->rhs) {
		fputs(" ", cfile->file);
		dump_expr(cfile, expr->rhs, verbose);
	}

	fputs(")", cfile->file);
}

/**
 * An open file for the command line.
 */
struct open_file {
	/** The file itself. */
	CFILE *cfile;
	/** The path to the file (for diagnostics). */
	const char *path;
};

/**
 * Free the parsed command line.
 */
int free_cmdline(struct cmdline *cmdline) {
	int ret = 0;

	if (cmdline) {
		CFILE *cout = cmdline->cout;
		CFILE *cerr = cmdline->cerr;

		free_expr(cmdline->expr);

		free_bfs_mtab(cmdline->mtab);

		bfs_free_groups(cmdline->groups);
		bfs_free_users(cmdline->users);

		struct trie_leaf *leaf;
		while ((leaf = trie_first_leaf(&cmdline->open_files))) {
			struct open_file *ofile = leaf->value;

			if (cfclose(ofile->cfile) != 0) {
				if (cerr) {
					bfs_error(cmdline, "'%s': %m.\n", ofile->path);
				}
				ret = -1;
			}

			free(ofile);
			trie_remove(&cmdline->open_files, leaf);
		}
		trie_destroy(&cmdline->open_files);

		if (cout && fflush(cout->file) != 0) {
			if (cerr) {
				bfs_error(cmdline, "standard output: %m.\n");
			}
			ret = -1;
		}

		cfclose(cout);
		cfclose(cerr);

		free_colors(cmdline->colors);
		darray_free(cmdline->paths);
		free(cmdline->argv);
		free(cmdline);
	}

	return ret;
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
	struct cmdline *cmdline;
	/** The command line arguments being parsed. */
	char **argv;
	/** The name of this program. */
	const char *command;

	/** The current regex flags to use. */
	int regex_flags;

	/** Whether stdout is a terminal. */
	bool stdout_tty;
	/** Whether this session is interactive (stdin and stderr are each a terminal). */
	bool interactive;
	/** Whether -color or -nocolor has been passed. */
	enum use_color use_color;
	/** Whether a -print action is implied. */
	bool implicit_print;
	/** Whether the expression has started. */
	bool expr_started;
	/** Whether any non-option arguments have been encountered. */
	bool non_option_seen;
	/** Whether an information option like -help or -version was passed. */
	bool just_info;

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
	bfs_verror(state->cmdline, format, args);
	va_end(args);
}

/**
 * Print a warning message during parsing.
 */
BFS_FORMATTER(2, 3)
static void parse_warning(const struct parser_state *state, const char *format, ...) {
	va_list args;
	va_start(args, format);
	bfs_vwarning(state->cmdline, format, args);
	va_end(args);
}

/**
 * Fill in a "-print"-type expression.
 */
static void init_print_expr(struct parser_state *state, struct expr *expr) {
	expr_set_always_true(expr);
	expr->cost = PRINT_COST;
	expr->cfile = state->cmdline->cout;
}

/**
 * Open a file for an expression.
 */
static int expr_open(struct parser_state *state, struct expr *expr, const char *path) {
	int ret = -1;

	struct cmdline *cmdline = state->cmdline;

	CFILE *cfile = cfopen(path, state->use_color ? cmdline->colors : NULL);
	if (!cfile) {
		parse_error(state, "${blu}%s${rs} ${bld}%s${rs}: %m.\n", expr->argv[0], path);
		goto out;
	}

	struct bfs_stat sb;
	if (bfs_stat(fileno(cfile->file), NULL, 0, &sb) != 0) {
		parse_error(state, "${blu}%s${rs} ${bld}%s${rs}: %m.\n", expr->argv[0], path);
		goto out_close;
	}

	bfs_file_id id;
	bfs_stat_id(&sb, &id);

	struct trie_leaf *leaf = trie_insert_mem(&cmdline->open_files, id, sizeof(id));
	if (!leaf) {
		perror("trie_insert_mem()");
		goto out_close;
	}

	if (leaf->value) {
		struct open_file *ofile = leaf->value;
		expr->cfile = ofile->cfile;
		ret = 0;
		goto out_close;
	}

	struct open_file *ofile = malloc(sizeof(*ofile));
	if (!ofile) {
		perror("malloc()");
		trie_remove(&cmdline->open_files, leaf);
		goto out_close;
	}

	ofile->cfile = cfile;
	ofile->path = path;
	leaf->value = ofile;
	++cmdline->nopen_files;

	expr->cfile = cfile;

	ret = 0;
	goto out;

out_close:
	cfclose(cfile);
out:
	return ret;
}

/**
 * Invoke bfs_stat() on an argument.
 */
static int stat_arg(const struct parser_state *state, struct expr *expr, struct bfs_stat *sb) {
	const struct cmdline *cmdline = state->cmdline;

	bool follow = cmdline->flags & (BFTW_COMFOLLOW | BFTW_LOGICAL);
	enum bfs_stat_flag flags = follow ? BFS_STAT_TRYFOLLOW : BFS_STAT_NOFOLLOW;

	int ret = bfs_stat(AT_FDCWD, expr->sdata, flags, sb);
	if (ret != 0) {
		parse_error(state, "${blu}%s${rs} ${bld}%s${rs}: %m.\n", expr->argv[0], expr->sdata);
	}
	return ret;
}

/**
 * Parse the expression specified on the command line.
 */
static struct expr *parse_expr(struct parser_state *state);

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
	struct cmdline *cmdline = state->cmdline;
	return DARRAY_PUSH(&cmdline->paths, &path);
}

/**
 * While parsing an expression, skip any paths and add them to the cmdline.
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
		goto bad;
	}

	if (endptr == str) {
		goto bad;
	}

	if (!(flags & IF_PARTIAL_OK) && *endptr != '\0') {
		goto bad;
	}

	if ((flags & IF_UNSIGNED) && value < 0) {
		goto bad;
	}

	switch (flags & IF_SIZE_MASK) {
	case IF_INT:
		if (value < INT_MIN || value > INT_MAX) {
			goto bad;
		}
		*(int *)result = value;
		break;

	case IF_LONG:
		if (value < LONG_MIN || value > LONG_MAX) {
			goto bad;
		}
		*(long *)result = value;
		break;

	case IF_LONG_LONG:
		*(long long *)result = value;
		break;

	default:
		assert(false);
		goto bad;
	}

	return endptr;

bad:
	if (!(flags & IF_QUIET)) {
		parse_error(state, "${bld}%s${rs} is not a valid integer.\n", str);
	}
	return NULL;
}

/**
 * Parse an integer and a comparison flag.
 */
static const char *parse_icmp(const struct parser_state *state, const char *str, struct expr *expr, enum int_flags flags) {
	switch (str[0]) {
	case '-':
		expr->cmp_flag = CMP_LESS;
		++str;
		break;
	case '+':
		expr->cmp_flag = CMP_GREATER;
		++str;
		break;
	default:
		expr->cmp_flag = CMP_EXACT;
		break;
	}

	return parse_int(state, str, &expr->idata, flags | IF_LONG_LONG | IF_UNSIGNED);
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
static struct expr *parse_flag(struct parser_state *state, size_t argc) {
	parser_advance(state, T_FLAG, argc);
	return &expr_true;
}

/**
 * Parse a flag that doesn't take a value.
 */
static struct expr *parse_nullary_flag(struct parser_state *state) {
	return parse_flag(state, 1);
}

/**
 * Parse a flag that takes a single value.
 */
static struct expr *parse_unary_flag(struct parser_state *state) {
	return parse_flag(state, 2);
}

/**
 * Parse a single option.
 */
static struct expr *parse_option(struct parser_state *state, size_t argc) {
	const char *arg = *parser_advance(state, T_OPTION, argc);

	if (state->non_option_seen) {
		parse_warning(state,
		              "The ${blu}%s${rs} option applies to the entire command line.  For clarity, place\n"
		              "it before any non-option arguments.\n\n",
		              arg);
	}

	return &expr_true;
}

/**
 * Parse an option that doesn't take a value.
 */
static struct expr *parse_nullary_option(struct parser_state *state) {
	return parse_option(state, 1);
}

/**
 * Parse an option that takes a value.
 */
static struct expr *parse_unary_option(struct parser_state *state) {
	return parse_option(state, 2);
}

/**
 * Parse a single positional option.
 */
static struct expr *parse_positional_option(struct parser_state *state, size_t argc) {
	parser_advance(state, T_OPTION, argc);
	return &expr_true;
}

/**
 * Parse a positional option that doesn't take a value.
 */
static struct expr *parse_nullary_positional_option(struct parser_state *state) {
	return parse_positional_option(state, 1);
}

/**
 * Parse a positional option that takes a single value.
 */
static struct expr *parse_unary_positional_option(struct parser_state *state) {
	return parse_positional_option(state, 2);
}

/**
 * Parse a single test.
 */
static struct expr *parse_test(struct parser_state *state, eval_fn *eval, size_t argc) {
	char **argv = parser_advance(state, T_TEST, argc);
	struct expr *expr = new_expr(eval, argc, argv);
	if (expr) {
		expr->pure = true;
	}
	return expr;
}

/**
 * Parse a test that doesn't take a value.
 */
static struct expr *parse_nullary_test(struct parser_state *state, eval_fn *eval) {
	return parse_test(state, eval, 1);
}

/**
 * Parse a test that takes a value.
 */
static struct expr *parse_unary_test(struct parser_state *state, eval_fn *eval) {
	const char *arg = state->argv[0];
	const char *value = state->argv[1];
	if (!value) {
		parse_error(state, "${blu}%s${rs} needs a value.\n", arg);
		return NULL;
	}

	struct expr *expr = parse_test(state, eval, 2);
	if (expr) {
		expr->sdata = value;
	}
	return expr;
}

/**
 * Parse a single action.
 */
static struct expr *parse_action(struct parser_state *state, eval_fn *eval, size_t argc) {
	if (eval != eval_nohidden && eval != eval_prune && eval != eval_quit) {
		state->implicit_print = false;
	}

	char **argv = parser_advance(state, T_ACTION, argc);
	return new_expr(eval, argc, argv);
}

/**
 * Parse an action that takes no arguments.
 */
static struct expr *parse_nullary_action(struct parser_state *state, eval_fn *eval) {
	return parse_action(state, eval, 1);
}

/**
 * Parse an action that takes one argument.
 */
static struct expr *parse_unary_action(struct parser_state *state, eval_fn *eval) {
	const char *arg = state->argv[0];
	const char *value = state->argv[1];
	if (!value) {
		parse_error(state, "${blu}%s${rs} needs a value.\n", arg);
		return NULL;
	}

	struct expr *expr = parse_action(state, eval, 2);
	if (expr) {
		expr->sdata = value;
	}
	return expr;
}

/**
 * Parse a test expression with integer data and a comparison flag.
 */
static struct expr *parse_test_icmp(struct parser_state *state, eval_fn *eval) {
	struct expr *expr = parse_unary_test(state, eval);
	if (!expr) {
		return NULL;
	}

	if (!parse_icmp(state, expr->sdata, expr, 0)) {
		free_expr(expr);
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

/** A named debug flag. */
struct debug_flag {
	enum debug_flags flag;
	const char *name;
};

/** The table of debug flags. */
struct debug_flag debug_flags[] = {
	{DEBUG_ALL,    "all"},
	{DEBUG_COST,   "cost"},
	{DEBUG_EXEC,   "exec"},
	{DEBUG_OPT,    "opt"},
	{DEBUG_RATES,  "rates"},
	{DEBUG_SEARCH, "search"},
	{DEBUG_STAT,   "stat"},
	{DEBUG_TREE,   "tree"},
	{0},
};

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
static struct expr *parse_debug(struct parser_state *state, int arg1, int arg2) {
	struct cmdline *cmdline = state->cmdline;

	const char *arg = state->argv[0];
	const char *flags = state->argv[1];
	if (!flags) {
		parse_error(state, "${cyn}%s${rs} needs a flag.\n\n", arg);
		debug_help(cmdline->cerr);
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
			debug_help(cmdline->cout);
			state->just_info = true;
			return NULL;
		}

		for (int i = 0; ; ++i) {
			const char *expected = debug_flags[i].name;
			if (!expected) {
				if (cmdline->warn) {
					parse_warning(state, "Unrecognized debug flag ${bld}");
					fwrite(flag, 1, len, stderr);
					cfprintf(cmdline->cerr, "${rs}.\n\n");
					unrecognized = true;
				}
				break;
			}

			if (parse_debug_flag(flag, len, expected)) {
				cmdline->debug |= debug_flags[i].flag;
				break;
			}
		}
	}

	if (unrecognized) {
		debug_help(cmdline->cerr);
		cfprintf(cmdline->cerr, "\n");
	}

	return parse_unary_flag(state);
}

/**
 * Parse -On.
 */
static struct expr *parse_optlevel(struct parser_state *state, int arg1, int arg2) {
	int *optlevel = &state->cmdline->optlevel;

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
static struct expr *parse_follow(struct parser_state *state, int flags, int option) {
	struct cmdline *cmdline = state->cmdline;
	cmdline->flags &= ~(BFTW_COMFOLLOW | BFTW_LOGICAL);
	cmdline->flags |= flags;
	if (option) {
		return parse_nullary_positional_option(state);
	} else {
		return parse_nullary_flag(state);
	}
}

/**
 * Parse -X.
 */
static struct expr *parse_xargs_safe(struct parser_state *state, int arg1, int arg2) {
	state->cmdline->xargs_safe = true;
	return parse_nullary_flag(state);
}

/**
 * Parse -executable, -readable, -writable
 */
static struct expr *parse_access(struct parser_state *state, int flag, int arg2) {
	struct expr *expr = parse_nullary_test(state, eval_access);
	if (!expr) {
		return NULL;
	}

	expr->idata = flag;
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
static struct expr *parse_acl(struct parser_state *state, int flag, int arg2) {
#if BFS_CAN_CHECK_ACL
	struct expr *expr = parse_nullary_test(state, eval_acl);
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
static struct expr *parse_newer(struct parser_state *state, int field, int arg2) {
	struct expr *expr = parse_unary_test(state, eval_newer);
	if (!expr) {
		return NULL;
	}

	struct bfs_stat sb;
	if (stat_arg(state, expr, &sb) != 0) {
		goto fail;
	}

	expr->cost = STAT_COST;
	expr->reftime = sb.mtime;
	expr->stat_field = field;
	return expr;

fail:
	free_expr(expr);
	return NULL;
}

/**
 * Parse -[aBcm]{min,time}.
 */
static struct expr *parse_time(struct parser_state *state, int field, int unit) {
	struct expr *expr = parse_test_icmp(state, eval_time);
	if (!expr) {
		return NULL;
	}

	expr->cost = STAT_COST;
	expr->reftime = state->now;
	expr->stat_field = field;
	expr->time_unit = unit;
	return expr;
}

/**
 * Parse -capable.
 */
static struct expr *parse_capable(struct parser_state *state, int flag, int arg2) {
#if BFS_CAN_CHECK_CAPABILITIES
	struct expr *expr = parse_nullary_test(state, eval_capable);
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
static struct expr *parse_color(struct parser_state *state, int color, int arg2) {
	struct cmdline *cmdline = state->cmdline;
	struct colors *colors = cmdline->colors;
	if (color) {
		state->use_color = COLOR_ALWAYS;
		cmdline->cout->colors = colors;
		cmdline->cerr->colors = colors;
	} else {
		state->use_color = COLOR_NEVER;
		cmdline->cout->colors = NULL;
		cmdline->cerr->colors = NULL;
	}
	return parse_nullary_option(state);
}

/**
 * Parse -{false,true}.
 */
static struct expr *parse_const(struct parser_state *state, int value, int arg2) {
	parser_advance(state, T_TEST, 1);
	return value ? &expr_true : &expr_false;
}

/**
 * Parse -daystart.
 */
static struct expr *parse_daystart(struct parser_state *state, int arg1, int arg2) {
	struct tm tm;
	if (xlocaltime(&state->now.tv_sec, &tm) != 0) {
		perror("xlocaltime()");
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
		perror("xmktime()");
		return NULL;
	}

	state->now.tv_sec = time;
	state->now.tv_nsec = 0;

	return parse_nullary_positional_option(state);
}

/**
 * Parse -delete.
 */
static struct expr *parse_delete(struct parser_state *state, int arg1, int arg2) {
	state->cmdline->flags |= BFTW_DEPTH;
	state->depth_arg = state->argv[0];
	return parse_nullary_action(state, eval_delete);
}

/**
 * Parse -d.
 */
static struct expr *parse_depth(struct parser_state *state, int arg1, int arg2) {
	state->cmdline->flags |= BFTW_DEPTH;
	state->depth_arg = state->argv[0];
	return parse_nullary_flag(state);
}

/**
 * Parse -depth [N].
 */
static struct expr *parse_depth_n(struct parser_state *state, int arg1, int arg2) {
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
static struct expr *parse_depth_limit(struct parser_state *state, int is_min, int arg2) {
	struct cmdline *cmdline = state->cmdline;
	const char *arg = state->argv[0];
	const char *value = state->argv[1];
	if (!value) {
		parse_error(state, "${blu}%s${rs} needs a value.\n", arg);
		return NULL;
	}

	int *depth = is_min ? &cmdline->mindepth : &cmdline->maxdepth;
	if (!parse_int(state, value, depth, IF_INT | IF_UNSIGNED)) {
		return NULL;
	}

	return parse_unary_option(state);
}

/**
 * Parse -empty.
 */
static struct expr *parse_empty(struct parser_state *state, int arg1, int arg2) {
	struct expr *expr = parse_nullary_test(state, eval_empty);
	if (!expr) {
		return NULL;
	}

	expr->cost = 2000.0;
	expr->probability = 0.01;

	if (state->cmdline->optlevel < 4) {
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
static struct expr *parse_exec(struct parser_state *state, int flags, int arg2) {
	struct bfs_exec *execbuf = parse_bfs_exec(state->argv, flags, state->cmdline);
	if (!execbuf) {
		return NULL;
	}

	struct expr *expr = parse_action(state, eval_exec, execbuf->tmpl_argc + 2);
	if (!expr) {
		free_bfs_exec(execbuf);
		return NULL;
	}

	expr->execbuf = execbuf;

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

	return expr;
}

/**
 * Parse -exit [STATUS].
 */
static struct expr *parse_exit(struct parser_state *state, int arg1, int arg2) {
	size_t argc = 1;
	const char *value = state->argv[1];

	int status = EXIT_SUCCESS;
	if (value && parse_int(state, value, &status, IF_INT | IF_UNSIGNED | IF_QUIET)) {
		argc = 2;
	}

	struct expr *expr = parse_action(state, eval_exit, argc);
	if (expr) {
		expr_set_never_returns(expr);
		expr->idata = status;
	}
	return expr;
}

/**
 * Parse -f PATH.
 */
static struct expr *parse_f(struct parser_state *state, int arg1, int arg2) {
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
	return &expr_true;
}

/**
 * Parse -fls FILE.
 */
static struct expr *parse_fls(struct parser_state *state, int arg1, int arg2) {
	struct expr *expr = parse_unary_action(state, eval_fls);
	if (expr) {
		expr_set_always_true(expr);
		expr->cost = PRINT_COST;
		if (expr_open(state, expr, expr->sdata) != 0) {
			goto fail;
		}
		expr->reftime = state->now;
	}
	return expr;

fail:
	free_expr(expr);
	return NULL;
}

/**
 * Parse -fprint FILE.
 */
static struct expr *parse_fprint(struct parser_state *state, int arg1, int arg2) {
	struct expr *expr = parse_unary_action(state, eval_fprint);
	if (expr) {
		expr_set_always_true(expr);
		expr->cost = PRINT_COST;
		if (expr_open(state, expr, expr->sdata) != 0) {
			goto fail;
		}
	}
	return expr;

fail:
	free_expr(expr);
	return NULL;
}

/**
 * Parse -fprint0 FILE.
 */
static struct expr *parse_fprint0(struct parser_state *state, int arg1, int arg2) {
	struct expr *expr = parse_unary_action(state, eval_fprint0);
	if (expr) {
		expr_set_always_true(expr);
		expr->cost = PRINT_COST;
		if (expr_open(state, expr, expr->sdata) != 0) {
			goto fail;
		}
	}
	return expr;

fail:
	free_expr(expr);
	return NULL;
}

/**
 * Parse -fprintf FILE FORMAT.
 */
static struct expr *parse_fprintf(struct parser_state *state, int arg1, int arg2) {
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

	struct expr *expr = parse_action(state, eval_fprintf, 3);
	if (!expr) {
		return NULL;
	}

	expr_set_always_true(expr);

	expr->cost = PRINT_COST;

	if (expr_open(state, expr, file) != 0) {
		goto fail;
	}

	expr->printf = parse_bfs_printf(format, state->cmdline);
	if (!expr->printf) {
		goto fail;
	}

	return expr;

fail:
	free_expr(expr);
	return NULL;
}

/**
 * Parse -fstype TYPE.
 */
static struct expr *parse_fstype(struct parser_state *state, int arg1, int arg2) {
	struct cmdline *cmdline = state->cmdline;
	if (!cmdline->mtab) {
		parse_error(state, "Couldn't parse the mount table: %s.\n", strerror(cmdline->mtab_error));
		return NULL;
	}

	struct expr *expr = parse_unary_test(state, eval_fstype);
	if (expr) {
		expr->cost = STAT_COST;
	}
	return expr;
}

/**
 * Parse -gid/-group.
 */
static struct expr *parse_group(struct parser_state *state, int arg1, int arg2) {
	struct cmdline *cmdline = state->cmdline;
	if (!cmdline->groups) {
		parse_error(state, "Couldn't parse the group table: %s.\n", strerror(cmdline->groups_error));
		return NULL;
	}

	const char *arg = state->argv[0];

	struct expr *expr = parse_unary_test(state, eval_gid);
	if (!expr) {
		return NULL;
	}

	const struct group *grp = bfs_getgrnam(cmdline->groups, expr->sdata);
	if (grp) {
		expr->idata = grp->gr_gid;
		expr->cmp_flag = CMP_EXACT;
	} else if (looks_like_icmp(expr->sdata)) {
		if (!parse_icmp(state, expr->sdata, expr, 0)) {
			goto fail;
		}
	} else {
		parse_error(state, "${blu}%s${rs} ${bld}%s${rs}: No such group.\n", arg, expr->sdata);
		goto fail;
	}

	expr->cost = STAT_COST;

	return expr;

fail:
	free_expr(expr);
	return NULL;
}

/**
 * Parse -unique.
 */
static struct expr *parse_unique(struct parser_state *state, int arg1, int arg2) {
	state->cmdline->unique = true;
	return parse_nullary_option(state);
}

/**
 * Parse -used N.
 */
static struct expr *parse_used(struct parser_state *state, int arg1, int arg2) {
	struct expr *expr = parse_test_icmp(state, eval_used);
	if (expr) {
		expr->cost = STAT_COST;
	}
	return expr;
}

/**
 * Parse -uid/-user.
 */
static struct expr *parse_user(struct parser_state *state, int arg1, int arg2) {
	struct cmdline *cmdline = state->cmdline;
	if (!cmdline->users) {
		parse_error(state, "Couldn't parse the user table: %s.\n", strerror(cmdline->users_error));
		return NULL;
	}

	const char *arg = state->argv[0];

	struct expr *expr = parse_unary_test(state, eval_uid);
	if (!expr) {
		return NULL;
	}

	const struct passwd *pwd = bfs_getpwnam(cmdline->users, expr->sdata);
	if (pwd) {
		expr->idata = pwd->pw_uid;
		expr->cmp_flag = CMP_EXACT;
	} else if (looks_like_icmp(expr->sdata)) {
		if (!parse_icmp(state, expr->sdata, expr, 0)) {
			goto fail;
		}
	} else {
		parse_error(state, "${blu}%s${rs} ${bld}%s${rs}: No such user.\n", arg, expr->sdata);
		goto fail;
	}

	expr->cost = STAT_COST;

	return expr;

fail:
	free_expr(expr);
	return NULL;
}

/**
 * Parse -hidden.
 */
static struct expr *parse_hidden(struct parser_state *state, int arg1, int arg2) {
	struct expr *expr = parse_nullary_test(state, eval_hidden);
	if (expr) {
		expr->probability = 0.01;
	}
	return expr;
}

/**
 * Parse -(no)?ignore_readdir_race.
 */
static struct expr *parse_ignore_races(struct parser_state *state, int ignore, int arg2) {
	state->cmdline->ignore_races = ignore;
	return parse_nullary_option(state);
}

/**
 * Parse -inum N.
 */
static struct expr *parse_inum(struct parser_state *state, int arg1, int arg2) {
	struct expr *expr = parse_test_icmp(state, eval_inum);
	if (expr) {
		expr->cost = STAT_COST;
		expr->probability = expr->cmp_flag == CMP_EXACT ? 0.01 : 0.50;
	}
	return expr;
}

/**
 * Parse -links N.
 */
static struct expr *parse_links(struct parser_state *state, int arg1, int arg2) {
	struct expr *expr = parse_test_icmp(state, eval_links);
	if (expr) {
		expr->cost = STAT_COST;
		expr->probability = expr_cmp(expr, 1) ? 0.99 : 0.01;
	}
	return expr;
}

/**
 * Parse -ls.
 */
static struct expr *parse_ls(struct parser_state *state, int arg1, int arg2) {
	struct expr *expr = parse_nullary_action(state, eval_fls);
	if (expr) {
		init_print_expr(state, expr);
		expr->reftime = state->now;
	}
	return expr;
}

/**
 * Parse -mount.
 */
static struct expr *parse_mount(struct parser_state *state, int arg1, int arg2) {
	parse_warning(state,
	              "In the future, ${blu}%s${rs} will skip mount points entirely, unlike\n"
	              "${blu}-xdev${rs}, due to http://austingroupbugs.net/view.php?id=1133.\n\n",
	              state->argv[0]);

	state->cmdline->flags |= BFTW_XDEV;
	state->mount_arg = state->argv[0];
	return parse_nullary_option(state);
}

/**
 * Common code for fnmatch() tests.
 */
static struct expr *parse_fnmatch(const struct parser_state *state, struct expr *expr, bool casefold) {
	if (!expr) {
		return NULL;
	}

	if (casefold) {
#ifdef FNM_CASEFOLD
		expr->idata = FNM_CASEFOLD;
#else
		parse_error(state, "${blu}%s${rs} is missing platform support.\n", expr->argv[0]);
		free_expr(expr);
		return NULL;
#endif
	} else {
		expr->idata = 0;
	}

	expr->cost = 400.0;

	if (strchr(expr->sdata, '*')) {
		expr->probability = 0.5;
	} else {
		expr->probability = 0.1;
	}

	return expr;
}

/**
 * Parse -i?name.
 */
static struct expr *parse_name(struct parser_state *state, int casefold, int arg2) {
	struct expr *expr = parse_unary_test(state, eval_name);
	return parse_fnmatch(state, expr, casefold);
}

/**
 * Parse -i?path, -i?wholename.
 */
static struct expr *parse_path(struct parser_state *state, int casefold, int arg2) {
	struct expr *expr = parse_unary_test(state, eval_path);
	return parse_fnmatch(state, expr, casefold);
}

/**
 * Parse -i?lname.
 */
static struct expr *parse_lname(struct parser_state *state, int casefold, int arg2) {
	struct expr *expr = parse_unary_test(state, eval_lname);
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
static int parse_reftime(const struct parser_state *state, struct expr *expr) {
	if (parse_timestamp(expr->sdata, &expr->reftime) == 0) {
		return 0;
	} else if (errno != EINVAL) {
		parse_error(state, "${blu}%s${rs} ${bld}%s${rs}: %m.\n", expr->argv[0], expr->argv[1]);
		return -1;
	}

	parse_error(state, "${blu}%s${rs} ${bld}%s${rs}: Invalid timestamp.\n\n", expr->argv[0], expr->argv[1]);
	fprintf(stderr, "Supported timestamp formats are ISO 8601-like, e.g.\n\n");

	struct tm tm;
	if (xlocaltime(&state->now.tv_sec, &tm) != 0) {
		perror("xlocaltime()");
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
		perror("xgmtime()");
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
static struct expr *parse_newerxy(struct parser_state *state, int arg1, int arg2) {
	const char *arg = state->argv[0];
	if (strlen(arg) != 8) {
		parse_error(state, "Expected ${blu}-newer${bld}XY${rs}; found ${blu}-newer${bld}%s${rs}.\n", arg + 6);
		return NULL;
	}

	struct expr *expr = parse_unary_test(state, eval_newer);
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
		if (stat_arg(state, expr, &sb) != 0) {
			goto fail;
		}


		const struct timespec *reftime = bfs_stat_time(&sb, field);
		if (!reftime) {
			parse_error(state, "${blu}%s${rs} ${bld}%s${rs}: Couldn't get file %s.\n", arg, expr->sdata, bfs_stat_field_name(field));
			goto fail;
		}

		expr->reftime = *reftime;
	}

	expr->cost = STAT_COST;

	return expr;

fail:
	free_expr(expr);
	return NULL;
}

/**
 * Parse -nogroup.
 */
static struct expr *parse_nogroup(struct parser_state *state, int arg1, int arg2) {
	struct cmdline *cmdline = state->cmdline;
	if (!cmdline->groups) {
		parse_error(state, "Couldn't parse the group table: %s.\n", strerror(cmdline->groups_error));
		return NULL;
	}

	struct expr *expr = parse_nullary_test(state, eval_nogroup);
	if (expr) {
		expr->cost = 9000.0;
		expr->probability = 0.01;
	}
	return expr;
}

/**
 * Parse -nohidden.
 */
static struct expr *parse_nohidden(struct parser_state *state, int arg1, int arg2) {
	state->prune_arg = state->argv[0];
	return parse_nullary_action(state, eval_nohidden);
}

/**
 * Parse -noleaf.
 */
static struct expr *parse_noleaf(struct parser_state *state, int arg1, int arg2) {
	parse_warning(state, "${ex}bfs${rs} does not apply the optimization that ${blu}%s${rs} inhibits.\n\n", state->argv[0]);
	return parse_nullary_option(state);
}

/**
 * Parse -nouser.
 */
static struct expr *parse_nouser(struct parser_state *state, int arg1, int arg2) {
	struct cmdline *cmdline = state->cmdline;
	if (!cmdline->users) {
		parse_error(state, "Couldn't parse the user table: %s.\n", strerror(cmdline->users_error));
		return NULL;
	}

	struct expr *expr = parse_nullary_test(state, eval_nouser);
	if (expr) {
		expr->cost = 9000.0;
		expr->probability = 0.01;
	}
	return expr;
}

/**
 * Parse a permission mode like chmod(1).
 */
static int parse_mode(const struct parser_state *state, const char *mode, struct expr *expr) {
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
			// Fallthrough

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
				// Fallthrough
			case MODE_PLUS:
				expr->file_mode |= file_change;
				expr->dir_mode |= dir_change;
				break;
			case MODE_MINUS:
				expr->file_mode &= ~file_change;
				expr->dir_mode &= ~dir_change;
				break;
			}
			// Fallthrough

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
				// Fallthrough
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
				file_change |= S_ISVTX;
				dir_change |= S_ISVTX;
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
static struct expr *parse_perm(struct parser_state *state, int field, int arg2) {
	struct expr *expr = parse_unary_test(state, eval_perm);
	if (!expr) {
		return NULL;
	}

	const char *mode = expr->sdata;
	switch (mode[0]) {
	case '-':
		expr->mode_cmp = MODE_ALL;
		++mode;
		break;
	case '/':
		expr->mode_cmp = MODE_ANY;
		++mode;
		break;
	case '+':
		if (mode[1] >= '0' && mode[1] <= '9') {
			expr->mode_cmp = MODE_ANY;
			++mode;
			break;
		}
		// Fallthrough
	default:
		expr->mode_cmp = MODE_EXACT;
		break;
	}

	if (parse_mode(state, mode, expr) != 0) {
		goto fail;
	}

	expr->cost = STAT_COST;

	return expr;

fail:
	free_expr(expr);
	return NULL;
}

/**
 * Parse -print.
 */
static struct expr *parse_print(struct parser_state *state, int arg1, int arg2) {
	struct expr *expr = parse_nullary_action(state, eval_fprint);
	if (expr) {
		init_print_expr(state, expr);
	}
	return expr;
}

/**
 * Parse -print0.
 */
static struct expr *parse_print0(struct parser_state *state, int arg1, int arg2) {
	struct expr *expr = parse_nullary_action(state, eval_fprint0);
	if (expr) {
		init_print_expr(state, expr);
	}
	return expr;
}

/**
 * Parse -printf FORMAT.
 */
static struct expr *parse_printf(struct parser_state *state, int arg1, int arg2) {
	struct expr *expr = parse_unary_action(state, eval_fprintf);
	if (!expr) {
		return NULL;
	}

	init_print_expr(state, expr);

	expr->printf = parse_bfs_printf(expr->sdata, state->cmdline);
	if (!expr->printf) {
		free_expr(expr);
		return NULL;
	}

	return expr;
}

/**
 * Parse -printx.
 */
static struct expr *parse_printx(struct parser_state *state, int arg1, int arg2) {
	struct expr *expr = parse_nullary_action(state, eval_fprintx);
	if (expr) {
		init_print_expr(state, expr);
	}
	return expr;
}

/**
 * Parse -prune.
 */
static struct expr *parse_prune(struct parser_state *state, int arg1, int arg2) {
	state->prune_arg = state->argv[0];

	struct expr *expr = parse_nullary_action(state, eval_prune);
	if (expr) {
		expr_set_always_true(expr);
	}
	return expr;
}

/**
 * Parse -quit.
 */
static struct expr *parse_quit(struct parser_state *state, int arg1, int arg2) {
	struct expr *expr = parse_nullary_action(state, eval_quit);
	if (expr) {
		expr_set_never_returns(expr);
	}
	return expr;
}

/**
 * Parse -i?regex.
 */
static struct expr *parse_regex(struct parser_state *state, int flags, int arg2) {
	struct expr *expr = parse_unary_test(state, eval_regex);
	if (!expr) {
		goto fail;
	}

	expr->regex = malloc(sizeof(regex_t));
	if (!expr->regex) {
		perror("malloc()");
		goto fail;
	}

	int err = regcomp(expr->regex, expr->sdata, state->regex_flags | flags);
	if (err != 0) {
		char *str = xregerror(err, expr->regex);
		if (str) {
			parse_error(state, "${blu}%s${rs} ${bld}%s${rs}: %s.\n", expr->argv[0], expr->argv[1], str);
			free(str);
		} else {
			perror("xregerror()");
		}
		goto fail_regex;
	}

	return expr;

fail_regex:
	free(expr->regex);
	expr->regex = NULL;
fail:
	free_expr(expr);
	return NULL;
}

/**
 * Parse -E.
 */
static struct expr *parse_regex_extended(struct parser_state *state, int arg1, int arg2) {
	state->regex_flags = REG_EXTENDED;
	return parse_nullary_flag(state);
}

/**
 * Parse -regextype TYPE.
 */
static struct expr *parse_regextype(struct parser_state *state, int arg1, int arg2) {
	struct cmdline *cmdline = state->cmdline;
	CFILE *cfile = cmdline->cerr;

	const char *arg = state->argv[0];
	const char *type = state->argv[1];
	if (!type) {
		parse_error(state, "${blu}%s${rs} needs a value.\n\n", arg);
		goto list_types;
	}

	if (strcmp(type, "posix-basic") == 0) {
		state->regex_flags = 0;
	} else if (strcmp(type, "posix-extended") == 0) {
		state->regex_flags = REG_EXTENDED;
	} else if (strcmp(type, "help") == 0) {
		state->just_info = true;
		cfile = cmdline->cout;
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
	return NULL;
}

/**
 * Parse -s.
 */
static struct expr *parse_s(struct parser_state *state, int arg1, int arg2) {
	state->cmdline->flags |= BFTW_SORT;
	return parse_nullary_flag(state);
}

/**
 * Parse -samefile FILE.
 */
static struct expr *parse_samefile(struct parser_state *state, int arg1, int arg2) {
	struct expr *expr = parse_unary_test(state, eval_samefile);
	if (!expr) {
		return NULL;
	}

	struct bfs_stat sb;
	if (stat_arg(state, expr, &sb) != 0) {
		free_expr(expr);
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
static struct expr *parse_search_strategy(struct parser_state *state, int arg1, int arg2) {
	struct cmdline *cmdline = state->cmdline;
	CFILE *cfile = cmdline->cerr;

	const char *flag = state->argv[0];
	const char *arg = state->argv[1];
	if (!arg) {
		parse_error(state, "${cyn}%s${rs} needs an argument.\n\n", flag);
		goto list_strategies;
	}


	if (strcmp(arg, "bfs") == 0) {
		cmdline->strategy = BFTW_BFS;
	} else if (strcmp(arg, "dfs") == 0) {
		cmdline->strategy = BFTW_DFS;
	} else if (strcmp(arg, "ids") == 0) {
		cmdline->strategy = BFTW_IDS;
	} else if (strcmp(arg, "help") == 0) {
		state->just_info = true;
		cfile = cmdline->cout;
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
	return NULL;
}

/**
 * Parse -[aBcm]?since.
 */
static struct expr *parse_since(struct parser_state *state, int field, int arg2) {
	struct expr *expr = parse_unary_test(state, eval_newer);
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
	free_expr(expr);
	return NULL;
}

/**
 * Parse -size N[cwbkMGTP]?.
 */
static struct expr *parse_size(struct parser_state *state, int arg1, int arg2) {
	struct expr *expr = parse_unary_test(state, eval_size);
	if (!expr) {
		return NULL;
	}

	const char *unit = parse_icmp(state, expr->sdata, expr, IF_PARTIAL_OK);
	if (!unit) {
		goto fail;
	}

	if (strlen(unit) > 1) {
		goto bad_unit;
	}

	switch (*unit) {
	case '\0':
	case 'b':
		expr->size_unit = SIZE_BLOCKS;
		break;
	case 'c':
		expr->size_unit = SIZE_BYTES;
		break;
	case 'w':
		expr->size_unit = SIZE_WORDS;
		break;
	case 'k':
		expr->size_unit = SIZE_KB;
		break;
	case 'M':
		expr->size_unit = SIZE_MB;
		break;
	case 'G':
		expr->size_unit = SIZE_GB;
		break;
	case 'T':
		expr->size_unit = SIZE_TB;
		break;
	case 'P':
		expr->size_unit = SIZE_PB;
		break;

	default:
		goto bad_unit;
	}

	expr->cost = STAT_COST;
	expr->probability = expr->cmp_flag == CMP_EXACT ? 0.01 : 0.50;

	return expr;

bad_unit:
	parse_error(state, "${blu}%s${rs} ${bld}%s${rs}: Expected a size unit (one of ${bld}cwbkMGTP${rs}); found ${er}%s${rs}.\n",
	            expr->argv[0], expr->argv[1], unit);
fail:
	free_expr(expr);
	return NULL;
}

/**
 * Parse -sparse.
 */
static struct expr *parse_sparse(struct parser_state *state, int arg1, int arg2) {
	struct expr *expr = parse_nullary_test(state, eval_sparse);
	if (expr) {
		expr->cost = STAT_COST;
	}
	return expr;
}

/**
 * Parse -x?type [bcdpflsD].
 */
static struct expr *parse_type(struct parser_state *state, int x, int arg2) {
	eval_fn *eval = x ? eval_xtype : eval_type;
	struct expr *expr = parse_unary_test(state, eval);
	if (!expr) {
		return NULL;
	}

	enum bftw_typeflag types = 0;
	double probability = 0.0;

	const char *c = expr->sdata;
	while (true) {
		enum bftw_typeflag type;
		double type_prob;

		switch (*c) {
		case 'b':
			type = BFTW_BLK;
			type_prob = 0.00000721183;
			break;
		case 'c':
			type = BFTW_CHR;
			type_prob = 0.0000499855;
			break;
		case 'd':
			type = BFTW_DIR;
			type_prob = 0.114475;
			break;
		case 'D':
			type = BFTW_DOOR;
			type_prob = 0.000001;
			break;
		case 'p':
			type = BFTW_FIFO;
			type_prob = 0.00000248684;
			break;
		case 'f':
			type = BFTW_REG;
			type_prob = 0.859772;
			break;
		case 'l':
			type = BFTW_LNK;
			type_prob = 0.0256816;
			break;
		case 's':
			type = BFTW_SOCK;
			type_prob = 0.0000116881;
			break;
		case 'w':
			type = BFTW_WHT;
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

		if (!(types & type)) {
			types |= type;
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

	expr->idata = types;
	expr->probability = probability;

	if (x && state->cmdline->optlevel < 4) {
		// Since -xtype dereferences symbolic links, it may have side
		// effects such as reporting permission errors, and thus
		// shouldn't be re-ordered without aggressive optimizations
		expr->pure = false;
	}

	return expr;

fail:
	free_expr(expr);
	return NULL;
}

/**
 * Parse -(no)?warn.
 */
static struct expr *parse_warn(struct parser_state *state, int warn, int arg2) {
	state->cmdline->warn = warn;
	return parse_nullary_positional_option(state);
}

/**
 * Parse -xattr.
 */
static struct expr *parse_xattr(struct parser_state *state, int arg1, int arg2) {
#if BFS_CAN_CHECK_XATTRS
	struct expr *expr = parse_nullary_test(state, eval_xattr);
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
static struct expr *parse_xdev(struct parser_state *state, int arg1, int arg2) {
	state->cmdline->flags |= BFTW_XDEV;
	state->xdev_arg = state->argv[0];
	return parse_nullary_option(state);
}

/**
 * Launch a pager for the help output.
 */
static CFILE *launch_pager(pid_t *pid, CFILE *cout) {
	char *pager = getenv("PAGER");
	if (!pager) {
		pager = "more";
	}

	int pipefd[2];
	if (pipe(pipefd) != 0) {
		goto fail;
	}

	FILE *file = fdopen(pipefd[1], "w");
	if (!file) {
		goto fail_pipe;
	}
	pipefd[1] = -1;

	CFILE *ret = cfdup(file, NULL);
	if (!ret) {
		goto fail_file;
	}
	file = NULL;
	ret->close = true;
	ret->colors = cout->colors;

	struct bfs_spawn ctx;
	if (bfs_spawn_init(&ctx) != 0) {
		goto fail_ret;
	}

	if (bfs_spawn_setflags(&ctx, BFS_SPAWN_USEPATH) != 0) {
		goto fail_ctx;
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
		pager,
		NULL,
	};

	extern char **environ;
	char **envp = environ;

	if (!getenv("LESS")) {
		size_t envc;
		for (envc = 0; environ[envc]; ++envc);
		++envc;

		envp = malloc((envc + 1)*sizeof(*envp));
		if (!envp) {
			goto fail_ctx;
		}

		memcpy(envp, environ, (envc - 1)*sizeof(*envp));
		envp[envc - 1] = "LESS=FKRX";
		envp[envc] = NULL;
	}

	*pid = bfs_spawn(pager, &ctx, argv, envp);
	if (*pid < 0) {
		goto fail_envp;
	}

	close(pipefd[0]);
	if (envp != environ) {
		free(envp);
	}
	bfs_spawn_destroy(&ctx);
	return ret;

fail_envp:
	if (envp != environ) {
		free(envp);
	}
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
		close(pipefd[1]);
	}
	if (pipefd[0] >= 0) {
		close(pipefd[0]);
	}
fail:
	return cout;
}

/**
 * "Parse" -help.
 */
static struct expr *parse_help(struct parser_state *state, int arg1, int arg2) {
	CFILE *cout = state->cmdline->cout;

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
	cfprintf(cout, "      Enable optimization level ${bld}N${rs} (default: 3)\n");
	cfprintf(cout, "  ${cyn}-S${rs} ${bld}bfs${rs}|${bld}dfs${rs}|${bld}ids${rs}\n");
	cfprintf(cout, "      Use ${bld}b${rs}readth-${bld}f${rs}irst/${bld}d${rs}epth-${bld}f${rs}irst/${bld}i${rs}terative ${bld}d${rs}eepening ${bld}s${rs}earch (default: ${cyn}-S${rs} ${bld}bfs${rs})\n\n");

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

	cfprintf(cout, "${bld}Options:${rs}\n\n");

	cfprintf(cout, "  ${blu}-color${rs}\n");
	cfprintf(cout, "  ${blu}-nocolor${rs}\n");
	cfprintf(cout, "      Turn colors on or off (default: ${blu}-color${rs} if outputting to a terminal,\n");
	cfprintf(cout, "      ${blu}-nocolor${rs} otherwise)\n");
	cfprintf(cout, "  ${blu}-daystart${rs}\n");
	cfprintf(cout, "      Measure times relative to the start of today\n");
	cfprintf(cout, "  ${blu}-depth${rs}\n");
	cfprintf(cout, "      Search in post-order (descendents first)\n");
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
	cfprintf(cout, "  ${blu}-noleaf${rs}\n");
	cfprintf(cout, "      Ignored; for compatibility with GNU find\n");
	cfprintf(cout, "  ${blu}-regextype${rs} ${bld}TYPE${rs}\n");
	cfprintf(cout, "      Use ${bld}TYPE${rs}-flavored regexes (default: ${bld}posix-basic${rs}; see ${blu}-regextype${rs}"
	                " ${bld}help${rs})\n");
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
	cfprintf(cout, "  ${blu}-nohidden${rs}\n");
	cfprintf(cout, "      Find hidden files, or filter them out\n");
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
	cfprintf(cout, "  ${blu}-fprintf${rs} ${bld}FORMAT${rs} ${bld}FILE${rs}\n");
	cfprintf(cout, "      Like ${blu}-ls${rs}/${blu}-print${rs}/${blu}-print0${rs}/${blu}-printf${rs}, but write to ${bld}FILE${rs} instead of standard\n"
	               "      output\n");
	cfprintf(cout, "  ${blu}-ls${rs}\n");
	cfprintf(cout, "      List files like ${ex}ls${rs} ${bld}-dils${rs}\n");
	cfprintf(cout, "  ${blu}-nohidden${rs}\n");
	cfprintf(cout, "      Filter out hidden files and directories\n");
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
static struct expr *parse_version(struct parser_state *state, int arg1, int arg2) {
	cfprintf(state->cmdline->cout, "${ex}bfs${rs} ${bld}%s${rs}\n\n", BFS_VERSION);

	printf("%s\n", BFS_HOMEPAGE);

	state->just_info = true;
	return NULL;
}

typedef struct expr *parse_fn(struct parser_state *state, int arg1, int arg2);

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
	{"-Bmin", T_TEST, parse_time, BFS_STAT_BTIME, MINUTES},
	{"-Bnewer", T_TEST, parse_newer, BFS_STAT_BTIME},
	{"-Bsince", T_TEST, parse_since, BFS_STAT_BTIME},
	{"-Btime", T_TEST, parse_time, BFS_STAT_BTIME, DAYS},
	{"-D", T_FLAG, parse_debug},
	{"-E", T_FLAG, parse_regex_extended},
	{"-H", T_FLAG, parse_follow, BFTW_COMFOLLOW, false},
	{"-L", T_FLAG, parse_follow, BFTW_LOGICAL, false},
	{"-O", T_FLAG, parse_optlevel, 0, 0, true},
	{"-P", T_FLAG, parse_follow, 0, false},
	{"-S", T_FLAG, parse_search_strategy},
	{"-X", T_FLAG, parse_xargs_safe},
	{"-a", T_OPERATOR},
	{"-acl", T_TEST, parse_acl},
	{"-amin", T_TEST, parse_time, BFS_STAT_ATIME, MINUTES},
	{"-and", T_OPERATOR},
	{"-anewer", T_TEST, parse_newer, BFS_STAT_ATIME},
	{"-asince", T_TEST, parse_since, BFS_STAT_ATIME},
	{"-atime", T_TEST, parse_time, BFS_STAT_ATIME, DAYS},
	{"-capable", T_TEST, parse_capable},
	{"-cmin", T_TEST, parse_time, BFS_STAT_CTIME, MINUTES},
	{"-cnewer", T_TEST, parse_newer, BFS_STAT_CTIME},
	{"-color", T_OPTION, parse_color, true},
	{"-csince", T_TEST, parse_since, BFS_STAT_CTIME},
	{"-ctime", T_TEST, parse_time, BFS_STAT_CTIME, DAYS},
	{"-d", T_FLAG, parse_depth},
	{"-daystart", T_OPTION, parse_daystart},
	{"-delete", T_ACTION, parse_delete},
	{"-depth", T_OPTION, parse_depth_n},
	{"-empty", T_TEST, parse_empty},
	{"-exec", T_ACTION, parse_exec, 0},
	{"-execdir", T_ACTION, parse_exec, BFS_EXEC_CHDIR},
	{"-executable", T_TEST, parse_access, X_OK},
	{"-exit", T_ACTION, parse_exit},
	{"-f", T_FLAG, parse_f},
	{"-false", T_TEST, parse_const, false},
	{"-fls", T_ACTION, parse_fls},
	{"-follow", T_OPTION, parse_follow, BFTW_LOGICAL, true},
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
	{"-iregex", T_TEST, parse_regex, REG_ICASE},
	{"-iwholename", T_TEST, parse_path, true},
	{"-links", T_TEST, parse_links},
	{"-lname", T_TEST, parse_lname, false},
	{"-ls", T_ACTION, parse_ls},
	{"-maxdepth", T_OPTION, parse_depth_limit, false},
	{"-mindepth", T_OPTION, parse_depth_limit, true},
	{"-mmin", T_TEST, parse_time, BFS_STAT_MTIME, MINUTES},
	{"-mnewer", T_TEST, parse_newer, BFS_STAT_MTIME},
	{"-mount", T_OPTION, parse_mount},
	{"-msince", T_TEST, parse_since, BFS_STAT_MTIME},
	{"-mtime", T_TEST, parse_time, BFS_STAT_MTIME, DAYS},
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
	{"-nowarn", T_TEST, parse_warn, false},
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
	{"-true", T_TEST, parse_const, true},
	{"-type", T_TEST, parse_type, false},
	{"-uid", T_TEST, parse_user},
	{"-unique", T_ACTION, parse_unique},
	{"-used", T_TEST, parse_used},
	{"-user", T_TEST, parse_user},
	{"-version", T_ACTION, parse_version},
	{"-warn", T_OPTION, parse_warn, true},
	{"-wholename", T_TEST, parse_path, false},
	{"-writable", T_TEST, parse_access, W_OK},
	{"-x", T_FLAG, parse_xdev},
	{"-xattr", T_TEST, parse_xattr},
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
static struct expr *parse_literal(struct parser_state *state) {
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

	CFILE *cerr = state->cmdline->cerr;
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
 *        | LITERAL
 */
static struct expr *parse_factor(struct parser_state *state) {
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

		struct expr *expr = parse_expr(state);
		if (!expr) {
			return NULL;
		}

		if (skip_paths(state) != 0) {
			free_expr(expr);
			return NULL;
		}

		arg = state->argv[0];
		if (!arg || strcmp(arg, ")") != 0) {
			parse_error(state, "Expected a ${red})${rs} after ${blu}%s${rs}.\n", state->argv[-1]);
			free_expr(expr);
			return NULL;
		}
		parser_advance(state, T_OPERATOR, 1);

		return expr;
	} else if (strcmp(arg, "!") == 0 || strcmp(arg, "-not") == 0) {
		char **argv = parser_advance(state, T_OPERATOR, 1);

		struct expr *factor = parse_factor(state);
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
static struct expr *parse_term(struct parser_state *state) {
	struct expr *term = parse_factor(state);

	while (term) {
		if (skip_paths(state) != 0) {
			free_expr(term);
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

		struct expr *lhs = term;
		struct expr *rhs = parse_factor(state);
		if (!rhs) {
			free_expr(lhs);
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
static struct expr *parse_clause(struct parser_state *state) {
	struct expr *clause = parse_term(state);

	while (clause) {
		if (skip_paths(state) != 0) {
			free_expr(clause);
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

		struct expr *lhs = clause;
		struct expr *rhs = parse_term(state);
		if (!rhs) {
			free_expr(lhs);
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
static struct expr *parse_expr(struct parser_state *state) {
	struct expr *expr = parse_clause(state);

	while (expr) {
		if (skip_paths(state) != 0) {
			free_expr(expr);
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

		struct expr *lhs = expr;
		struct expr *rhs = parse_clause(state);
		if (!rhs) {
			free_expr(lhs);
			return NULL;
		}

		expr = new_binary_expr(eval_comma, lhs, rhs, argv);
	}

	return expr;
}

/**
 * Parse the top-level expression.
 */
static struct expr *parse_whole_expr(struct parser_state *state) {
	if (skip_paths(state) != 0) {
		return NULL;
	}

	struct expr *expr = &expr_true;
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
		struct expr *print = new_expr(eval_fprint, 1, &fake_print_arg);
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

	if (state->cmdline->warn && state->depth_arg && state->prune_arg) {
		parse_warning(state, "${blu}%s${rs} does not work in the presence of ${blu}%s${rs}.\n", state->prune_arg, state->depth_arg);

		if (state->interactive) {
			fprintf(stderr, "Do you want to continue? ");
			if (ynprompt() == 0) {
				goto fail;
			}
		}

		fprintf(stderr, "\n");
	}

	return expr;

fail:
	free_expr(expr);
	return NULL;
}

/**
 * Dump the parsed form of the command line, for debugging.
 */
void dump_cmdline(const struct cmdline *cmdline, bool verbose) {
	CFILE *cerr = cmdline->cerr;

	cfprintf(cerr, "${ex}%s${rs} ", cmdline->argv[0]);

	if (cmdline->flags & BFTW_LOGICAL) {
		cfprintf(cerr, "${cyn}-L${rs} ");
	} else if (cmdline->flags & BFTW_COMFOLLOW) {
		cfprintf(cerr, "${cyn}-H${rs} ");
	} else {
		cfprintf(cerr, "${cyn}-P${rs} ");
	}

	if (cmdline->xargs_safe) {
		cfprintf(cerr, "${cyn}-X${rs} ");
	}

	if (cmdline->flags & BFTW_SORT) {
		cfprintf(cerr, "${cyn}-s${rs} ");
	}

	if (cmdline->optlevel != 3) {
		cfprintf(cerr, "${cyn}-O%d${rs} ", cmdline->optlevel);
	}

	const char *strategy = NULL;
	switch (cmdline->strategy) {
	case BFTW_BFS:
		strategy = "bfs";
		break;
	case BFTW_DFS:
		strategy = "dfs";
		break;
	case BFTW_IDS:
		strategy = "ids";
		break;
	}
	assert(strategy);
	cfprintf(cerr, "${cyn}-S${rs} ${bld}%s${rs} ", strategy);

	enum debug_flags debug = cmdline->debug;
	if (debug) {
		cfprintf(cerr, "${cyn}-D${rs} ");
		for (int i = 0; debug; ++i) {
			enum debug_flags flag = debug_flags[i].flag;
			const char *name = debug_flags[i].name;
			if ((debug & flag) == flag) {
				cfprintf(cerr, "${bld}%s${rs}", name);
				debug ^= flag;
				if (debug) {
					cfprintf(cerr, ",");
				}
			}
		}
		cfprintf(cerr, " ");
	}

	for (size_t i = 0; i < darray_length(cmdline->paths); ++i) {
		const char *path = cmdline->paths[i];
		char c = path[0];
		if (c == '-' || c == '(' || c == ')' || c == '!' || c == ',') {
			cfprintf(cerr, "${cyn}-f${rs} ");
		}
		cfprintf(cerr, "${mag}%s${rs} ", path);
	}

	if (cmdline->cout->colors) {
		cfprintf(cerr, "${blu}-color${rs} ");
	} else {
		cfprintf(cerr, "${blu}-nocolor${rs} ");
	}
	if (cmdline->flags & BFTW_DEPTH) {
		cfprintf(cerr, "${blu}-depth${rs} ");
	}
	if (cmdline->ignore_races) {
		cfprintf(cerr, "${blu}-ignore_readdir_race${rs} ");
	}
	if (cmdline->mindepth != 0) {
		cfprintf(cerr, "${blu}-mindepth${rs} ${bld}%d${rs} ", cmdline->mindepth);
	}
	if (cmdline->maxdepth != INT_MAX) {
		cfprintf(cerr, "${blu}-maxdepth${rs} ${bld}%d${rs} ", cmdline->maxdepth);
	}
	if (cmdline->flags & BFTW_MOUNT) {
		cfprintf(cerr, "${blu}-mount${rs} ");
	}
	if (cmdline->unique) {
		cfprintf(cerr, "${blu}-unique${rs} ");
	}
	if ((cmdline->flags & (BFTW_MOUNT | BFTW_XDEV)) == BFTW_XDEV) {
		cfprintf(cerr, "${blu}-xdev${rs} ");
	}

	dump_expr(cerr, cmdline->expr, verbose);

	fputs("\n", stderr);
}

/**
 * Dump the estimated costs.
 */
static void dump_costs(const struct cmdline *cmdline) {
	CFILE *cerr = cmdline->cerr;
	const struct expr *expr = cmdline->expr;
	cfprintf(cerr, "       Cost: ~${ylw}%g${rs}\n", expr->cost);
	cfprintf(cerr, "Probability: ~${ylw}%g%%${rs}\n", 100.0*expr->probability);
}

/**
 * Get the current time.
 */
static int parse_gettime(struct timespec *ts) {
#if _POSIX_TIMERS > 0
	int ret = clock_gettime(CLOCK_REALTIME, ts);
	if (ret != 0) {
		perror("clock_gettime()");
	}
	return ret;
#else
	struct timeval tv;
	int ret = gettimeofday(&tv, NULL);
	if (ret == 0) {
		ts->tv_sec = tv.tv_sec;
		ts->tv_nsec = tv.tv_usec * 1000L;
	} else {
		perror("gettimeofday()");
	}
	return ret;
#endif
}

/**
 * Parse the command line.
 */
struct cmdline *parse_cmdline(int argc, char *argv[]) {
	struct cmdline *cmdline = malloc(sizeof(struct cmdline));
	if (!cmdline) {
		perror("malloc()");
		goto fail;
	}

	cmdline->argv = NULL;
	cmdline->paths = NULL;
	cmdline->colors = NULL;
	cmdline->cout = NULL;
	cmdline->cerr = NULL;
	cmdline->users = NULL;
	cmdline->users_error = 0;
	cmdline->groups = NULL;
	cmdline->groups_error = 0;
	cmdline->mtab = NULL;
	cmdline->mtab_error = 0;
	cmdline->mindepth = 0;
	cmdline->maxdepth = INT_MAX;
	cmdline->flags = BFTW_RECOVER;
	cmdline->strategy = BFTW_BFS;
	cmdline->optlevel = 3;
	cmdline->debug = 0;
	cmdline->ignore_races = false;
	cmdline->unique = false;
	cmdline->warn = false;
	cmdline->xargs_safe = false;
	cmdline->expr = &expr_true;
	cmdline->nopen_files = 0;

	trie_init(&cmdline->open_files);

	static char* default_argv[] = {"bfs", NULL};
	if (argc < 1) {
		argc = 1;
		argv = default_argv;
	}

	cmdline->argv = malloc((argc + 1)*sizeof(*cmdline->argv));
	if (!cmdline->argv) {
		perror("malloc()");
		goto fail;
	}
	for (int i = 0; i <= argc; ++i) {
		cmdline->argv[i] = argv[i];
	}

	enum use_color use_color = COLOR_AUTO;
	if (getenv("NO_COLOR")) {
		// https://no-color.org/
		use_color = COLOR_NEVER;
	}

	cmdline->colors = parse_colors(getenv("LS_COLORS"));
	cmdline->cout = cfdup(stdout, use_color ? cmdline->colors : NULL);
	cmdline->cerr = cfdup(stderr, use_color ? cmdline->colors : NULL);
	if (!cmdline->cout || !cmdline->cerr) {
		perror("cfdup()");
		goto fail;
	}

	cmdline->users = bfs_parse_users();
	if (!cmdline->users) {
		cmdline->users_error = errno;
	}

	cmdline->groups = bfs_parse_groups();
	if (!cmdline->groups) {
		cmdline->groups_error = errno;
	}

	cmdline->mtab = parse_bfs_mtab();
	if (!cmdline->mtab) {
		cmdline->mtab_error = errno;
	}

	bool stdin_tty = isatty(STDIN_FILENO);
	bool stdout_tty = isatty(STDOUT_FILENO);
	bool stderr_tty = isatty(STDERR_FILENO);

	if (!getenv("POSIXLY_CORRECT")) {
		cmdline->warn = stdin_tty;
	}

	struct parser_state state = {
		.cmdline = cmdline,
		.argv = cmdline->argv + 1,
		.command = cmdline->argv[0],
		.regex_flags = 0,
		.stdout_tty = stdout_tty,
		.interactive = stdin_tty && stderr_tty,
		.use_color = use_color,
		.implicit_print = true,
		.non_option_seen = false,
		.just_info = false,
		.last_arg = NULL,
		.depth_arg = NULL,
		.prune_arg = NULL,
		.mount_arg = NULL,
		.xdev_arg = NULL,
	};

	if (strcmp(xbasename(state.command), "find") == 0) {
		// Operate depth-first when invoked as "find"
		cmdline->strategy = BFTW_DFS;
	}

	if (parse_gettime(&state.now) != 0) {
		goto fail;
	}

	cmdline->expr = parse_whole_expr(&state);
	if (!cmdline->expr) {
		if (state.just_info) {
			goto done;
		} else {
			goto fail;
		}
	}

	if (optimize_cmdline(cmdline) != 0) {
		goto fail;
	}

	if (darray_length(cmdline->paths) == 0) {
		if (parse_root(&state, ".") != 0) {
			goto fail;
		}
	}

	if ((cmdline->flags & BFTW_LOGICAL) && !cmdline->unique) {
		// We need bftw() to detect cycles unless -unique does it for us
		cmdline->flags |= BFTW_DETECT_CYCLES;
	}

	if (cmdline->debug & DEBUG_TREE) {
		dump_cmdline(cmdline, false);
	}
	if (cmdline->debug & DEBUG_COST) {
		dump_costs(cmdline);
	}

done:
	return cmdline;

fail:
	free_cmdline(cmdline);
	return NULL;
}
