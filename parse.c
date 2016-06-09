/*********************************************************************
 * bfs                                                               *
 * Copyright (C) 2015-2016 Tavian Barnes <tavianator@tavianator.com> *
 *                                                                   *
 * This program is free software. It comes without any warranty, to  *
 * the extent permitted by applicable law. You can redistribute it   *
 * and/or modify it under the terms of the Do What The Fuck You Want *
 * To Public License, Version 2, as published by Sam Hocevar. See    *
 * the COPYING file or http://www.wtfpl.net/ for more details.       *
 *********************************************************************/

#include "bfs.h"
#include <ctype.h>
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
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

// Strings printed by -D tree for "fake" expressions
static char *fake_and_arg = "-a";
static char *fake_false_arg = "-false";
static char *fake_or_arg = "-o";
static char *fake_print_arg = "-print";
static char *fake_true_arg = "-true";

/**
 * Singleton true expression instance.
 */
static struct expr expr_true = {
	.eval = eval_true,
	.lhs = NULL,
	.rhs = NULL,
	.pure = true,
	.argc = 1,
	.argv = &fake_true_arg,
};

/**
 * Singleton false expression instance.
 */
static struct expr expr_false = {
	.eval = eval_false,
	.lhs = NULL,
	.rhs = NULL,
	.pure = true,
	.argc = 1,
	.argv = &fake_false_arg,
};

/**
 * Free an expression.
 */
static void free_expr(struct expr *expr) {
	if (expr && expr != &expr_true && expr != &expr_false) {
		if (expr->file && expr->file != stdout && expr->file != stderr) {
			if (fclose(expr->file) != 0) {
				perror("fclose()");
			}
		}

		free_expr(expr->lhs);
		free_expr(expr->rhs);
		free(expr);
	}
}

/**
 * Create a new expression.
 */
static struct expr *new_expr(eval_fn *eval, bool pure, size_t argc, char **argv) {
	struct expr *expr = malloc(sizeof(struct expr));
	if (expr) {
		expr->eval = eval;
		expr->lhs = NULL;
		expr->rhs = NULL;
		expr->pure = pure;
		expr->argc = argc;
		expr->argv = argv;
		expr->file = NULL;
	}
	return expr;
}

/**
 * Create a new unary expression.
 */
static struct expr *new_unary_expr(eval_fn *eval, struct expr *rhs, char **argv) {
	struct expr *expr = new_expr(eval, rhs->pure, 1, argv);
	if (!expr) {
		free_expr(rhs);
		return NULL;
	}

	expr->rhs = rhs;
	return expr;
}

/**
 * Create a new binary expression.
 */
static struct expr *new_binary_expr(eval_fn *eval, struct expr *lhs, struct expr *rhs, char **argv) {
	struct expr *expr = new_expr(eval, lhs->pure && rhs->pure, 1, argv);
	if (!expr) {
		free_expr(rhs);
		free_expr(lhs);
		return NULL;
	}

	expr->lhs = lhs;
	expr->rhs = rhs;
	return expr;
}

/**
 * Dump the parsed expression tree, for debugging.
 */
static void dump_expr(const struct expr *expr) {
	fputs("(", stderr);

	for (size_t i = 0; i < expr->argc; ++i) {
		if (i > 0) {
			fputs(" ", stderr);
		}
		fputs(expr->argv[i], stderr);
	}

	if (expr->lhs) {
		fputs(" ", stderr);
		dump_expr(expr->lhs);
	}

	if (expr->rhs) {
		fputs(" ", stderr);
		dump_expr(expr->rhs);
	}

	fputs(")", stderr);
}

/**
 * Free the parsed command line.
 */
void free_cmdline(struct cmdline *cmdline) {
	if (cmdline) {
		free_expr(cmdline->expr);
		free_colors(cmdline->colors);
		free(cmdline->roots);
		free(cmdline);
	}
}

/**
 * Add a root path to the cmdline.
 */
static bool cmdline_add_root(struct cmdline *cmdline, const char *root) {
	size_t i = cmdline->nroots++;
	const char **roots = realloc(cmdline->roots, cmdline->nroots*sizeof(const char *));
	if (!roots) {
		perror("realloc()");
		return false;
	}

	roots[i] = root;
	cmdline->roots = roots;
	return true;
}

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

	/** The optimization level. */
	int optlevel;

	/** Whether a -print action is implied. */
	bool implicit_print;
	/** Whether warnings are enabled (see -warn, -nowarn). */
	bool warn;
	/** Whether the expression has started. */
	bool expr_started;
	/** Whether any non-option arguments have been encountered. */
	bool non_option_seen;
	/** Whether an information option like -help or -version was passed. */
	bool just_info;

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
 * Log an optimization.
 */
static void debug_opt(const struct parser_state *state, const char *format, ...) {
	if (!(state->cmdline->debug & DEBUG_OPT)) {
		return;
	}

	va_list args;
	va_start(args, format);

	for (const char *i = format; *i != '\0'; ++i) {
		if (*i == '%') {
			switch (*++i) {
			case '%':
				fputc('%', stderr);
				break;

			case 's':
				fputs(va_arg(args, const char *), stderr);
				break;

			case 'e':
				dump_expr(va_arg(args, const struct expr *));
				break;
			}
		} else {
			fputc(*i, stderr);
		}
	}

	va_end(args);
}

/**
 * Invoke stat() on an argument.
 */
static int stat_arg(const struct parser_state *state, struct expr *expr, struct stat *sb) {
	const struct cmdline *cmdline = state->cmdline;

	bool follow = cmdline->flags & BFTW_FOLLOW;
	int flags = follow ? 0 : AT_SYMLINK_NOFOLLOW;

	int ret = fstatat(AT_FDCWD, expr->sdata, sb, flags);
	if (ret != 0) {
		pretty_error(cmdline->stderr_colors,
		             "error: '%s': %s\n", expr->sdata, strerror(errno));
		free_expr(expr);
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

	char **argv = state->argv;
	state->argv += argc;
	return argv;
}

/**
 * While parsing an expression, skip any paths and add them to the cmdline.
 */
static const char *skip_paths(struct parser_state *state) {
	while (true) {
		const char *arg = state->argv[0];

		// By POSIX, these are always options
		if (!arg
		    || (arg[0] == '-' && arg[1])
		    || strcmp(arg, "(") == 0
		    || strcmp(arg, "!") == 0) {
			return arg;
		}

		if (state->expr_started) {
			// By POSIX, these can be paths.  We only treat them as
			// such at the beginning of the command line
			if (strcmp(arg, ")") == 0 || strcmp(arg, ",") == 0) {
				return arg;
			}
		}

		if (!cmdline_add_root(state->cmdline, arg)) {
			return NULL;
		}

		parser_advance(state, T_PATH, 1);
	}
}

/** Integer parsing flags. */
enum intflags {
	IF_INT         = 0,
	IF_LONG        = 1,
	IF_LONG_LONG   = 2,
	IF_SIZE_MASK   = 0x3,
	IF_UNSIGNED    = 1 << 2,
	IF_PARTIAL_OK  = 1 << 3,
};

/**
 * Parse an integer.
 */
static const char *parse_int(const struct parser_state *state, const char *str, void *result, enum intflags flags) {
	char *endptr;

	errno = 0;
	long long value = strtoll(str, &endptr, 10);
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
	}

	return endptr;

bad:
	pretty_error(state->cmdline->stderr_colors,
	             "error: '%s' is not a valid integer.\n", str);
	return NULL;
}

/**
 * Parse an integer and a comparison flag.
 */
static const char *parse_icmp(const struct parser_state *state, const char *str, struct expr *expr, enum intflags flags) {
	switch (str[0]) {
	case '-':
		expr->cmpflag = CMP_LESS;
		++str;
		break;
	case '+':
		expr->cmpflag = CMP_GREATER;
		++str;
		break;
	default:
		expr->cmpflag = CMP_EXACT;
		break;
	}

	return parse_int(state, str, &expr->idata, flags | IF_LONG_LONG | IF_UNSIGNED);
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

	if (state->warn && state->non_option_seen) {
		pretty_warning(state->cmdline->stderr_colors,
		               "warning: The '%s' option applies to the entire command line.  For clarity, place\n"
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
 * Parse a single test.
 */
static struct expr *parse_test(struct parser_state *state, eval_fn *eval, size_t argc) {
	char **argv = parser_advance(state, T_TEST, argc);
	return new_expr(eval, true, argc, argv);
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
		pretty_error(state->cmdline->stderr_colors,
		             "error: %s needs a value.\n", arg);
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
	if (eval != eval_nohidden && eval != eval_prune) {
		state->implicit_print = false;
	}

	char **argv = parser_advance(state, T_ACTION, argc);
	return new_expr(eval, false, argc, argv);
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
		pretty_error(state->cmdline->stderr_colors,
		             "error: %s needs a value.\n", arg);
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
 * Parse -D FLAG.
 */
static struct expr *parse_debug(struct parser_state *state) {
	struct cmdline *cmdline = state->cmdline;

	const char *arg = state->argv[0];
	const char *flag = state->argv[1];
	if (!flag) {
		pretty_error(cmdline->stderr_colors,
		             "error: %s needs a flag.\n", arg);
		return NULL;
	}

	if (strcmp(flag, "help") == 0) {
		printf("Supported debug flags:\n\n");

		printf("  help:  This message.\n");
		printf("  opt:   Print optimization details.\n");
		printf("  stat:  Trace all stat() calls.\n");
		printf("  tree:  Print the parse tree.\n");

		state->just_info = true;
		return NULL;
	} else if (strcmp(flag, "opt") == 0) {
		cmdline->debug |= DEBUG_OPT;
	} else if (strcmp(flag, "stat") == 0) {
		cmdline->debug |= DEBUG_STAT;
	} else if (strcmp(flag, "tree") == 0) {
		cmdline->debug |= DEBUG_TREE;
	} else {
		pretty_warning(cmdline->stderr_colors,
		               "warning: Unrecognized debug flag '%s'.\n\n", flag);
	}

	return parse_unary_flag(state);
}

/**
 * Parse -On.
 */
static struct expr *parse_optlevel(struct parser_state *state) {
	int *optlevel = &state->cmdline->optlevel;

	if (strcmp(state->argv[0], "-Ofast") == 0) {
		*optlevel = 4;
	} else if (!parse_int(state, state->argv[0] + 2, optlevel, IF_INT)) {
		return NULL;
	}

	if (*optlevel > 4) {
		pretty_warning(state->cmdline->stderr_colors,
		               "warning: %s is the same as -O4.\n\n",
		               state->argv[0]);
	}

	return parse_nullary_flag(state);
}

/**
 * Parse -executable, -readable, -writable
 */
static struct expr *parse_access(struct parser_state *state, int flag) {
	struct expr *expr = parse_nullary_test(state, eval_access);
	if (expr) {
		expr->idata = flag;
	}
	return expr;
}

/**
 * Parse -[acm]{min,time}.
 */
static struct expr *parse_acmtime(struct parser_state *state, enum timefield field, enum timeunit unit) {
	struct expr *expr = parse_test_icmp(state, eval_acmtime);
	if (expr) {
		expr->reftime = state->now;
		expr->timefield = field;
		expr->timeunit = unit;
	}
	return expr;
}

/**
 * Parse -[ac]?newer.
 */
static struct expr *parse_acnewer(struct parser_state *state, enum timefield field) {
	struct expr *expr = parse_unary_test(state, eval_acnewer);
	if (!expr) {
		return NULL;
	}

	struct stat sb;
	if (stat_arg(state, expr, &sb) != 0) {
		return NULL;
	}

	expr->reftime = sb.st_mtim;
	expr->timefield = field;

	return expr;
}

/**
 * "Parse" -daystart.
 */
static struct expr *parse_daystart(struct parser_state *state) {
	// Should be called before localtime_r() according to POSIX.1-2004
	tzset();

	struct tm tm;
	if (!localtime_r(&state->now.tv_sec, &tm)) {
		perror("localtime_r()");
		return NULL;
	}

	if (tm.tm_hour || tm.tm_min || tm.tm_sec || state->now.tv_nsec) {
		++tm.tm_mday;
	}
	tm.tm_hour = 0;
	tm.tm_min = 0;
	tm.tm_sec = 0;

	time_t time = mktime(&tm);
	if (time == -1) {
		perror("mktime()");
		return NULL;
	}

	state->now.tv_sec = time;
	state->now.tv_nsec = 0;

	return parse_nullary_positional_option(state);
}

/**
 * Parse -{min,max}depth N.
 */
static struct expr *parse_depth(struct parser_state *state, int *depth) {
	const char *arg = state->argv[0];
	const char *value = state->argv[1];
	if (!value) {
		pretty_error(state->cmdline->stderr_colors,
		             "error: %s needs a value.\n", arg);
		return NULL;
	}

	if (!parse_int(state, value, depth, IF_INT)) {
		return NULL;
	}

	return parse_unary_option(state);
}

/**
 * Parse -exec[dir]/-ok[dir].
 */
static struct expr *parse_exec(struct parser_state *state, enum execflags flags) {
	size_t i = 1;
	const char *arg;
	while ((arg = state->argv[i++])) {
		if (strcmp(arg, ";") == 0) {
			break;
		} else if (strcmp(arg, "+") == 0) {
			flags |= EXEC_MULTI;
			break;
		}
	}

	if (!arg) {
		pretty_error(state->cmdline->stderr_colors,
		             "error: %s: Expected ';' or '+'.\n", state->argv[0]);
		return NULL;
	}

	if (flags & EXEC_MULTI) {
		pretty_error(state->cmdline->stderr_colors,
		             "error: %s ... {} + is not supported yet.\n", state->argv[0]);
		return NULL;
	}

	struct expr *expr = parse_action(state, eval_exec, i);
	if (expr) {
		expr->execflags = flags;
	}
	return expr;
}

/**
 * Open a file for an expression.
 */
static int expr_open(struct parser_state *state, struct expr *expr, const char *path) {
	expr->file = fopen(path, "wb");
	if (!expr->file) {
		pretty_error(state->cmdline->stderr_colors,
		             "error: '%s': %s\n", path, strerror(errno));
		free_expr(expr);
		return -1;
	}

	++state->cmdline->nopen_files;
	return 0;
}

/**
 * Parse -fprint FILE.
 */
static struct expr *parse_fprint(struct parser_state *state) {
	struct expr *expr = parse_unary_action(state, eval_fprint);
	if (expr) {
		if (expr_open(state, expr, expr->sdata) != 0) {
			return NULL;
		}
	}
	return expr;
}

/**
 * Parse -fprint0 FILE.
 */
static struct expr *parse_fprint0(struct parser_state *state) {
	struct expr *expr = parse_unary_action(state, eval_print0);
	if (expr) {
		if (expr_open(state, expr, expr->sdata) != 0) {
			return NULL;
		}
	}
	return expr;
}

/**
 * Parse -print0.
 */
static struct expr *parse_print0(struct parser_state *state) {
	struct expr *expr = parse_nullary_action(state, eval_print0);
	if (expr) {
		expr->file = stdout;
	}
	return expr;
}

/**
 * Parse -group.
 */
static struct expr *parse_group(struct parser_state *state) {
	const char *arg = state->argv[0];

	struct expr *expr = parse_unary_test(state, eval_gid);
	if (!expr) {
		return NULL;
	}

	const char *error;

	errno = 0;
	struct group *grp = getgrnam(expr->sdata);
	if (grp) {
		expr->idata = grp->gr_gid;
	} else if (errno != 0) {
		error = strerror(errno);
		goto error;
	} else if (isdigit(expr->sdata[0])) {
		if (!parse_int(state, expr->sdata, &expr->idata, IF_LONG_LONG)) {
			goto fail;
		}
	} else {
		error = "No such group";
		goto error;
	}

	expr->cmpflag = CMP_EXACT;
	return expr;

error:
	pretty_error(state->cmdline->stderr_colors,
	             "error: %s %s: %s.\n", arg, expr->sdata, error);

fail:
	free_expr(expr);
	return NULL;
}

/**
 * Parse -user.
 */
static struct expr *parse_user(struct parser_state *state) {
	const char *arg = state->argv[0];

	struct expr *expr = parse_unary_test(state, eval_uid);
	if (!expr) {
		return NULL;
	}

	const char *error;

	errno = 0;
	struct passwd *pwd = getpwnam(expr->sdata);
	if (pwd) {
		expr->idata = pwd->pw_uid;
	} else if (errno != 0) {
		error = strerror(errno);
		goto error;
	} else if (isdigit(expr->sdata[0])) {
		if (!parse_int(state, expr->sdata, &expr->idata, IF_LONG_LONG)) {
			goto fail;
		}
	} else {
		error = "No such user";
		goto error;
	}

	expr->cmpflag = CMP_EXACT;
	return expr;

error:
	pretty_error(state->cmdline->stderr_colors,
	             "error: %s %s: %s.\n", arg, expr->sdata, error);

fail:
	free_expr(expr);
	return NULL;
}

/**
 * Set the FNM_CASEFOLD flag, if supported.
 */
static struct expr *set_fnm_casefold(const struct parser_state *state, struct expr *expr, bool casefold) {
	if (expr) {
		if (casefold) {
#ifdef FNM_CASEFOLD
			expr->idata = FNM_CASEFOLD;
#else
			pretty_error(state->cmdline->stderr_colors,
			             "error: %s is missing platform support.\n", expr->argv[0]);
			free(expr);
			return NULL;
#endif
		} else {
			expr->idata = 0;
		}
	}
	return expr;
}

/**
 * Parse -i?name.
 */
static struct expr *parse_name(struct parser_state *state, bool casefold) {
	struct expr *expr = parse_unary_test(state, eval_name);
	return set_fnm_casefold(state, expr, casefold);
}

/**
 * Parse -i?path, -i?wholename.
 */
static struct expr *parse_path(struct parser_state *state, bool casefold) {
	struct expr *expr = parse_unary_test(state, eval_path);
	return set_fnm_casefold(state, expr, casefold);
}

/**
 * Parse -i?lname.
 */
static struct expr *parse_lname(struct parser_state *state, bool casefold) {
	struct expr *expr = parse_unary_test(state, eval_lname);
	return set_fnm_casefold(state, expr, casefold);
}

/**
 * Parse -newerXY.
 */
static struct expr *parse_newerxy(struct parser_state *state) {
	const char *arg = state->argv[0];
	if (strlen(arg) != 8) {
		pretty_error(state->cmdline->stderr_colors,
		             "error: Expected -newerXY; found %s.\n", arg);
		return NULL;
	}

	struct expr *expr = parse_unary_test(state, eval_acnewer);
	if (!expr) {
		return NULL;
	}

	switch (arg[6]) {
	case 'a':
		expr->timefield = ATIME;
		break;
	case 'c':
		expr->timefield = CTIME;
		break;
	case 'm':
		expr->timefield = MTIME;
		break;

	case 'B':
		pretty_error(state->cmdline->stderr_colors,
		             "error: %s: File birth times ('B') are not supported.\n", arg);
		free(expr);
		return NULL;

	default:
		pretty_error(state->cmdline->stderr_colors,
		             "error: %s: For -newerXY, X should be 'a', 'c', 'm', or 'B'.\n", arg);
		free(expr);
		return NULL;
	}

	if (arg[7] == 't') {
		pretty_error(state->cmdline->stderr_colors,
		             "error: %s: Explicit reference times ('t') are not supported.\n", arg);
		free(expr);
		return NULL;
	} else {
		struct stat sb;
		if (stat_arg(state, expr, &sb) != 0) {
			return NULL;
		}

		switch (arg[7]) {
		case 'a':
			expr->reftime = sb.st_atim;
			break;
		case 'c':
			expr->reftime = sb.st_ctim;
			break;
		case 'm':
			expr->reftime = sb.st_mtim;
			break;

		case 'B':
			pretty_error(state->cmdline->stderr_colors,
			             "error: %s: File birth times ('B') are not supported.\n", arg);
			free(expr);
			return NULL;

		default:
			pretty_error(state->cmdline->stderr_colors,
			             "error: %s: For -newerXY, Y should be 'a', 'c', 'm', 'B', or 't'.\n", arg);
			free(expr);
			return NULL;
		}
	}

	return expr;
}

/**
 * Parse -noleaf.
 */
static struct expr *parse_noleaf(struct parser_state *state) {
	if (state->warn) {
		pretty_warning(state->cmdline->stderr_colors,
		               "warning: bfs does not apply the optimization that %s inhibits.\n\n",
		               state->argv[0]);
	}

	return parse_nullary_option(state);
}

/**
 * Parse -samefile FILE.
 */
static struct expr *parse_samefile(struct parser_state *state) {
	struct expr *expr = parse_unary_test(state, eval_samefile);
	if (!expr) {
		return NULL;
	}

	struct stat sb;
	if (stat_arg(state, expr, &sb) != 0) {
		return NULL;
	}

	expr->dev = sb.st_dev;
	expr->ino = sb.st_ino;

	return expr;
}

/**
 * Parse -size N[bcwkMG]?.
 */
static struct expr *parse_size(struct parser_state *state) {
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
		expr->sizeunit = SIZE_BLOCKS;
		break;
	case 'c':
		expr->sizeunit = SIZE_BYTES;
		break;
	case 'w':
		expr->sizeunit = SIZE_WORDS;
		break;
	case 'k':
		expr->sizeunit = SIZE_KB;
		break;
	case 'M':
		expr->sizeunit = SIZE_MB;
		break;
	case 'G':
		expr->sizeunit = SIZE_GB;
		break;

	default:
		goto bad_unit;
	}

	return expr;

bad_unit:
	pretty_error(state->cmdline->stderr_colors,
	             "error: %s %s: Expected a size unit of 'b', 'c', 'w', 'k', 'M', or 'G'; found %s.\n",
	             expr->argv[0], expr->argv[1], unit);
fail:
	free(expr);
	return NULL;
}

/**
 * Parse -x?type [bcdpfls].
 */
static struct expr *parse_type(struct parser_state *state, eval_fn *eval) {
	struct expr *expr = parse_unary_test(state, eval);
	if (!expr) {
		return NULL;
	}

	int typeflag = BFTW_UNKNOWN;

	switch (expr->sdata[0]) {
	case 'b':
		typeflag = BFTW_BLK;
		break;
	case 'c':
		typeflag = BFTW_CHR;
		break;
	case 'd':
		typeflag = BFTW_DIR;
		break;
	case 'p':
		typeflag = BFTW_FIFO;
		break;
	case 'f':
		typeflag = BFTW_REG;
		break;
	case 'l':
		typeflag = BFTW_LNK;
		break;
	case 's':
		typeflag = BFTW_SOCK;
		break;
	}

	if (typeflag == BFTW_UNKNOWN || expr->sdata[1] != '\0') {
		pretty_error(state->cmdline->stderr_colors,
		             "error: Unknown type flag '%s'.\n", expr->sdata);
		free_expr(expr);
		return NULL;
	}

	expr->idata = typeflag;
	return expr;
}

/**
 * "Parse" -help.
 */
static struct expr *parse_help(struct parser_state *state) {
	printf("Usage: %s [arguments...]\n\n", state->command);

	printf("bfs is compatible with find; see find -help or man find for help with find-\n"
	       "compatible options :)\n\n");

	printf("Extra features:\n"
	       "  -color, -nocolor: Turn on or off file type colorization.\n\n"
	       "  -hidden, -nohidden: Match hidden files, or filter them out.\n\n");

	printf("%s\n", BFS_HOMEPAGE);

	state->just_info = true;
	return NULL;
}

/**
 * "Parse" -version.
 */
static struct expr *parse_version(struct parser_state *state) {
	printf("bfs %s\n\n", BFS_VERSION);

	printf("%s\n", BFS_HOMEPAGE);

	state->just_info = true;
	return NULL;
}

/**
 * LITERAL : OPTION
 *         | TEST
 *         | ACTION
 */
static struct expr *parse_literal(struct parser_state *state) {
	struct cmdline *cmdline = state->cmdline;

	// Paths are already skipped at this point
	const char *arg = state->argv[0];

	if (arg[0] != '-') {
		pretty_error(cmdline->stderr_colors,
		             "error: Expected a predicate; found '%s'.\n", arg);
		return NULL;
	}

	switch (arg[1]) {
	case 'D':
		if (strcmp(arg, "-D") == 0) {
			return parse_debug(state);
		}
		break;

	case 'O':
		return parse_optlevel(state);

	case 'P':
		if (strcmp(arg, "-P") == 0) {
			cmdline->flags &= ~(BFTW_FOLLOW | BFTW_DETECT_CYCLES);
			return parse_nullary_flag(state);
		}
		break;

	case 'H':
		if (strcmp(arg, "-H") == 0) {
			cmdline->flags &= ~(BFTW_FOLLOW_NONROOT | BFTW_DETECT_CYCLES);
			cmdline->flags |= BFTW_FOLLOW_ROOT;
			return parse_nullary_flag(state);
		}
		break;

	case 'L':
		if (strcmp(arg, "-L") == 0) {
			cmdline->flags |= BFTW_FOLLOW | BFTW_DETECT_CYCLES;
			return parse_nullary_flag(state);
		}
		break;

	case 'a':
		if (strcmp(arg, "-amin") == 0) {
			return parse_acmtime(state, ATIME, MINUTES);
		} else if (strcmp(arg, "-atime") == 0) {
			return parse_acmtime(state, ATIME, DAYS);
		} else if (strcmp(arg, "-anewer") == 0) {
			return parse_acnewer(state, ATIME);
		}
		break;

	case 'c':
		if (strcmp(arg, "-cmin") == 0) {
			return parse_acmtime(state, CTIME, MINUTES);
		} else if (strcmp(arg, "-ctime") == 0) {
			return parse_acmtime(state, CTIME, DAYS);
		} else if (strcmp(arg, "-cnewer") == 0) {
			return parse_acnewer(state, CTIME);
		} else if (strcmp(arg, "-color") == 0) {
			cmdline->stdout_colors = cmdline->colors;
			cmdline->stderr_colors = cmdline->colors;
			return parse_nullary_option(state);
		}
		break;

	case 'd':
		if (strcmp(arg, "-daystart") == 0) {
			return parse_daystart(state);
		} else if (strcmp(arg, "-delete") == 0) {
			cmdline->flags |= BFTW_DEPTH;
			return parse_nullary_action(state, eval_delete);
		} else if (strcmp(arg, "-d") == 0 || strcmp(arg, "-depth") == 0) {
			cmdline->flags |= BFTW_DEPTH;
			return parse_nullary_option(state);
		}
		break;

	case 'e':
		if (strcmp(arg, "-empty") == 0) {
			return parse_nullary_test(state, eval_empty);
		} else if (strcmp(arg, "-exec") == 0) {
			return parse_exec(state, 0);
		} else if (strcmp(arg, "-execdir") == 0) {
			return parse_exec(state, EXEC_CHDIR);
		} else if (strcmp(arg, "-executable") == 0) {
			return parse_access(state, X_OK);
		}
		break;

	case 'f':
		if (strcmp(arg, "-false") == 0) {
			parser_advance(state, T_TEST, 1);
			return &expr_false;
		} else if (strcmp(arg, "-follow") == 0) {
			cmdline->flags |= BFTW_FOLLOW | BFTW_DETECT_CYCLES;
			return parse_nullary_positional_option(state);
		} else if (strcmp(arg, "-fprint") == 0) {
			return parse_fprint(state);
		} else if (strcmp(arg, "-fprint0") == 0) {
			return parse_fprint0(state);
		}
		break;

	case 'g':
		if (strcmp(arg, "-gid") == 0) {
			return parse_test_icmp(state, eval_gid);
		} else if (strcmp(arg, "-group") == 0) {
			return parse_group(state);
		}
		break;

	case 'h':
		if (strcmp(arg, "-help") == 0) {
			return parse_help(state);
		} else if (strcmp(arg, "-hidden") == 0) {
			return parse_nullary_test(state, eval_hidden);
		}
		break;

	case 'i':
		if (strcmp(arg, "-ilname") == 0) {
			return parse_lname(state, true);
		} if (strcmp(arg, "-iname") == 0) {
			return parse_name(state, true);
		} else if (strcmp(arg, "-inum") == 0) {
			return parse_test_icmp(state, eval_inum);
		} else if (strcmp(arg, "-ipath") == 0 || strcmp(arg, "-iwholename") == 0) {
			return parse_path(state, true);
		}
		break;

	case 'l':
		if (strcmp(arg, "-links") == 0) {
			return parse_test_icmp(state, eval_links);
		} else if (strcmp(arg, "-lname") == 0) {
			return parse_lname(state, false);
		}
		break;

	case 'm':
		if (strcmp(arg, "-mindepth") == 0) {
			return parse_depth(state, &cmdline->mindepth);
		} else if (strcmp(arg, "-maxdepth") == 0) {
			return parse_depth(state, &cmdline->maxdepth);
		} else if (strcmp(arg, "-mmin") == 0) {
			return parse_acmtime(state, MTIME, MINUTES);
		} else if (strcmp(arg, "-mount") == 0) {
			cmdline->flags |= BFTW_MOUNT;
			return parse_nullary_option(state);
		} else if (strcmp(arg, "-mtime") == 0) {
			return parse_acmtime(state, MTIME, DAYS);
		}
		break;

	case 'n':
		if (strcmp(arg, "-name") == 0) {
			return parse_name(state, false);
		} else if (strcmp(arg, "-newer") == 0) {
			return parse_acnewer(state, MTIME);
		} else if (strncmp(arg, "-newer", 6) == 0) {
			return parse_newerxy(state);
		} else if (strcmp(arg, "-nocolor") == 0) {
			cmdline->stdout_colors = NULL;
			cmdline->stderr_colors = NULL;
			return parse_nullary_option(state);
		} else if (strcmp(arg, "-nohidden") == 0) {
			return parse_nullary_action(state, eval_nohidden);
		} else if (strcmp(arg, "-noleaf") == 0) {
			return parse_noleaf(state);
		} else if (strcmp(arg, "-nowarn") == 0) {
			state->warn = false;
			return parse_nullary_positional_option(state);
		}
		break;

	case 'o':
		if (strcmp(arg, "-ok") == 0) {
			return parse_exec(state, EXEC_CONFIRM);
		} else if (strcmp(arg, "-okdir") == 0) {
			return parse_exec(state, EXEC_CONFIRM | EXEC_CHDIR);
		}
		break;

	case 'p':
		if (strcmp(arg, "-path") == 0) {
			return parse_path(state, false);
		} else if (strcmp(arg, "-print") == 0) {
			return parse_nullary_action(state, eval_print);
		} else if (strcmp(arg, "-print0") == 0) {
			return parse_print0(state);
		} else if (strcmp(arg, "-prune") == 0) {
			return parse_nullary_action(state, eval_prune);
		}
		break;

	case 'q':
		if (strcmp(arg, "-quit") == 0) {
			return parse_nullary_action(state, eval_quit);
		}
		break;

	case 'r':
		if (strcmp(arg, "-readable") == 0) {
			return parse_access(state, R_OK);
		}
		break;

	case 's':
		if (strcmp(arg, "-samefile") == 0) {
			return parse_samefile(state);
		} else if (strcmp(arg, "-size") == 0) {
			return parse_size(state);
		}
		break;

	case 't':
		if (strcmp(arg, "-true") == 0) {
			parser_advance(state, T_TEST, 1);
			return &expr_true;
		} else if (strcmp(arg, "-type") == 0) {
			return parse_type(state, eval_type);
		}
		break;

	case  'u':
		if (strcmp(arg, "-uid") == 0) {
			return parse_test_icmp(state, eval_uid);
		} else if (strcmp(arg, "-used") == 0) {
			return parse_test_icmp(state, eval_used);
		} else if (strcmp(arg, "-user") == 0) {
			return parse_user(state);
		}
		break;

	case 'v':
		if (strcmp(arg, "-version") == 0) {
			return parse_version(state);
		}
		break;

	case 'w':
		if (strcmp(arg, "-warn") == 0) {
			state->warn = true;
			return parse_nullary_positional_option(state);
		} else if (strcmp(arg, "-wholename") == 0) {
			return parse_path(state, false);
		} else if (strcmp(arg, "-writable") == 0) {
			return parse_access(state, W_OK);
		}
		break;

	case 'x':
		if (strcmp(arg, "-xdev") == 0) {
			cmdline->flags |= BFTW_MOUNT;
			return parse_nullary_option(state);
		} else if (strcmp(arg, "-xtype") == 0) {
			return parse_type(state, eval_xtype);
		}
		break;

	case '-':
		if (strcmp(arg, "--help") == 0) {
			return parse_help(state);
		} else if (strcmp(arg, "--version") == 0) {
			return parse_version(state);
		}
		break;
	}

	pretty_error(cmdline->stderr_colors,
	             "error: Unknown argument '%s'.\n", arg);
	return NULL;
}

static struct expr *new_and_expr(const struct parser_state *state, struct expr *lhs, struct expr *rhs, char **argv);
static struct expr *new_or_expr(const struct parser_state *state, struct expr *lhs, struct expr *rhs, char **argv);

/**
 * Create a "not" expression.
 */
static struct expr *new_not_expr(const struct parser_state *state, struct expr *rhs, char **argv) {
	if (state->cmdline->optlevel >= 1) {
		if (rhs == &expr_true) {
			debug_opt(state, "-O1: constant propagation: (%s %e) <==> %e\n", argv[0], rhs, &expr_false);
			return &expr_false;
		} else if (rhs == &expr_false) {
			debug_opt(state, "-O1: constant propagation: (%s %e) <==> %e\n", argv[0], rhs, &expr_true);
			return &expr_true;
		} else if (rhs->eval == eval_not) {
			struct expr *expr = rhs->rhs;
			debug_opt(state, "-O1: double negation: (%s %e) <==> %e\n", argv[0], rhs, expr);
			rhs->rhs = NULL;
			free_expr(rhs);
			return expr;
		} else if ((rhs->eval == eval_and || rhs->eval == eval_or)
			   && (rhs->lhs->eval == eval_not || rhs->rhs->eval == eval_not)) {
			bool other_and = rhs->eval == eval_or;
			char **other_arg = other_and ? &fake_and_arg : &fake_or_arg;

			debug_opt(state, "-O1: De Morgan's laws: (%s %e) <==> (%s (%s %e) (%s %e))\n",
				  argv[0], rhs,
				  *other_arg, argv[0], rhs->lhs, argv[0], rhs->rhs);

			struct expr *other_lhs = new_not_expr(state, rhs->lhs, argv);
			struct expr *other_rhs = new_not_expr(state, rhs->rhs, argv);
			rhs->rhs = NULL;
			rhs->lhs = NULL;
			free_expr(rhs);
			if (!other_lhs || !other_rhs) {
				free_expr(other_rhs);
				free_expr(other_lhs);
				return NULL;
			}

			if (other_and) {
				return new_and_expr(state, other_lhs, other_rhs, other_arg);
			} else {
				return new_or_expr(state, other_lhs, other_rhs, other_arg);
			}
		}
	}

	return new_unary_expr(eval_not, rhs, argv);
}

/**
 * FACTOR : "(" EXPR ")"
 *        | "!" FACTOR | "-not" FACTOR
 *        | LITERAL
 */
static struct expr *parse_factor(struct parser_state *state) {
	const char *arg = skip_paths(state);
	if (!arg) {
		fputs("Expression terminated prematurely.\n", stderr);
		return NULL;
	}

	if (strcmp(arg, "(") == 0) {
		parser_advance(state, T_OPERATOR, 1);

		struct expr *expr = parse_expr(state);
		if (!expr) {
			return NULL;
		}

		arg = skip_paths(state);
		if (!arg || strcmp(arg, ")") != 0) {
			fputs("Expected a ')'.\n", stderr);
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

		return new_not_expr(state, factor, argv);
	} else {
		return parse_literal(state);
	}
}

/**
 * Create an "and" expression.
 */
static struct expr *new_and_expr(const struct parser_state *state, struct expr *lhs, struct expr *rhs, char **argv) {
	int optlevel = state->cmdline->optlevel;
	if (optlevel >= 1) {
		if (lhs == &expr_true) {
			debug_opt(state, "-O1: conjunction elimination: (%s %e %e) <==> %e\n", argv[0], lhs, rhs, rhs);
			return rhs;
		} else if (lhs == &expr_false) {
			debug_opt(state, "-O1: short-circuit: (%s %e %e) <==> %e\n", argv[0], lhs, rhs, lhs);
			free_expr(rhs);
			return lhs;
		} else if (rhs == &expr_true) {
			debug_opt(state, "-O1: conjunction elimination: (%s %e %e) <==> %e\n", argv[0], lhs, rhs, lhs);
			return lhs;
		} else if (optlevel >= 2 && rhs == &expr_false && lhs->pure) {
			debug_opt(state, "-O2: purity: (%s %e %e) <==> %e\n", argv[0], lhs, rhs, rhs);
			free_expr(lhs);
			return rhs;
		} else if (lhs->eval == eval_not && rhs->eval == eval_not) {
			char **not_arg = lhs->argv;
			debug_opt(state, "-O1: De Morgan's laws: (%s %e %e) <==> (%s (%s %e %e))\n",
			          argv[0], lhs, rhs,
				  *not_arg, fake_or_arg, lhs->rhs, rhs->rhs);

			struct expr *or_expr = new_or_expr(state, lhs->rhs, rhs->rhs, &fake_or_arg);
			rhs->rhs = NULL;
			lhs->rhs = NULL;
			free_expr(rhs);
			free_expr(lhs);
			if (!or_expr) {
				return NULL;
			}

			return new_not_expr(state, or_expr, not_arg);
		}
	}

	return new_binary_expr(eval_and, lhs, rhs, argv);
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
		const char *arg = skip_paths(state);
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

		term = new_and_expr(state, lhs, rhs, argv);
	}

	return term;
}

/**
 * Create an "or" expression.
 */
static struct expr *new_or_expr(const struct parser_state *state, struct expr *lhs, struct expr *rhs, char **argv) {
	int optlevel = state->cmdline->optlevel;
	if (optlevel >= 1) {
		if (lhs == &expr_true) {
			debug_opt(state, "-O1: short-circuit: (%s %e %e) <==> %e\n", argv[0], lhs, rhs, lhs);
			free_expr(rhs);
			return lhs;
		} else if (lhs == &expr_false) {
			debug_opt(state, "-O1: disjunctive syllogism: (%s %e %e) <==> %e\n", argv[0], lhs, rhs, rhs);
			return rhs;
		} else if (optlevel >= 2 && rhs == &expr_true && lhs->pure) {
			debug_opt(state, "-O2: purity: (%s %e %e) <==> %e\n", argv[0], lhs, rhs, rhs);
			free_expr(lhs);
			return rhs;
		} else if (rhs == &expr_false) {
			debug_opt(state, "-O1: disjunctive syllogism: (%s %e %e) <==> %e\n", argv[0], lhs, rhs, lhs);
			return lhs;
		} else if (lhs->eval == eval_not && rhs->eval == eval_not) {
			char **not_arg = lhs->argv;
			debug_opt(state, "-O1: De Morgan's laws: (%s %e %e) <==> (%s (%s %e %e))\n",
			          argv[0], lhs, rhs,
				  *not_arg, fake_and_arg, lhs->rhs, rhs->rhs);

			struct expr *and_expr = new_and_expr(state, lhs->rhs, rhs->rhs, &fake_and_arg);
			rhs->rhs = NULL;
			lhs->rhs = NULL;
			free_expr(rhs);
			free_expr(lhs);
			if (!and_expr) {
				return NULL;
			}

			return new_not_expr(state, and_expr, not_arg);
		}
	}

	return new_binary_expr(eval_or, lhs, rhs, argv);
}

/**
 * CLAUSE : TERM
 *        | CLAUSE "-o" TERM
 *        | CLAUSE "-or" TERM
 */
static struct expr *parse_clause(struct parser_state *state) {
	struct expr *clause = parse_term(state);

	while (clause) {
		const char *arg = skip_paths(state);
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

		clause = new_or_expr(state, lhs, rhs, argv);
	}

	return clause;
}

/**
 * Create a "comma" expression.
 */
static struct expr *new_comma_expr(const struct parser_state *state, struct expr *lhs, struct expr *rhs, char **argv) {
	int optlevel = state->cmdline->optlevel;
	if (optlevel >= 1) {
		if (lhs->eval == eval_not) {
			debug_opt(state, "-O1: ignored result: (%s %e %e) <==> (%s %e %e)\n",
			          argv[0], lhs, rhs,
				  argv[0], lhs->rhs, rhs);

			struct expr *old = lhs;
			lhs = old->rhs;
			old->rhs = NULL;
			free_expr(old);
		}

		if (optlevel >= 2 && lhs->pure) {
			debug_opt(state, "-O2: purity: (%s %e %e) <==> %e\n", argv[0], lhs, rhs, rhs);
			free_expr(lhs);
			return rhs;
		}
	}

	return new_binary_expr(eval_comma, lhs, rhs, argv);
}

/**
 * EXPR : CLAUSE
 *      | EXPR "," CLAUSE
 */
static struct expr *parse_expr(struct parser_state *state) {
	struct expr *expr = parse_clause(state);

	while (expr) {
		const char *arg = skip_paths(state);
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

		expr = new_comma_expr(state, lhs, rhs, argv);
	}

	return expr;
}

/**
 * Apply top-level optimizations.
 */
static struct expr *optimize_whole_expr(const struct parser_state *state, struct expr *expr) {
	int optlevel = state->cmdline->optlevel;

	if (optlevel >= 2) {
		while ((expr->eval == eval_and || expr->eval == eval_or || expr->eval == eval_comma)
		       && expr->rhs->pure) {
			debug_opt(state, "-O2: top-level purity: %e <==> %e\n", expr, expr->lhs);

			struct expr *old = expr;
			expr = old->lhs;
			old->lhs = NULL;
			free_expr(old);
		}
	}

	if (optlevel >= 4 && expr->pure && expr != &expr_false) {
		debug_opt(state, "-O4: top-level purity: %e <==> %e\n", expr, &expr_false);
		free_expr(expr);
		expr = &expr_false;
	}

	return expr;
}

/**
 * Dump the parsed form of the command line, for debugging.
 */
static void dump_cmdline(const struct cmdline *cmdline) {
	if (cmdline->flags & BFTW_FOLLOW_NONROOT) {
		fputs("-L ", stderr);
	} else if (cmdline->flags & BFTW_FOLLOW_ROOT) {
		fputs("-H ", stderr);
	} else {
		fputs("-P ", stderr);
	}

	if (cmdline->optlevel != 3) {
		fprintf(stderr, "-O%d ", cmdline->optlevel);
	}

	if (cmdline->debug & DEBUG_OPT) {
		fputs("-D opt ", stderr);
	}
	if (cmdline->debug & DEBUG_STAT) {
		fputs("-D stat ", stderr);
	}
	if (cmdline->debug & DEBUG_TREE) {
		fputs("-D tree ", stderr);
	}

	for (size_t i = 0; i < cmdline->nroots; ++i) {
		fprintf(stderr, "%s ", cmdline->roots[i]);
	}

	if (cmdline->flags & BFTW_DEPTH) {
		fputs("-depth ", stderr);
	}
	if (cmdline->flags & BFTW_MOUNT) {
		fputs("-mount ", stderr);
	}
	if (cmdline->mindepth != 0) {
		fprintf(stderr, "-mindepth %d ", cmdline->mindepth);
	}
	if (cmdline->maxdepth != INT_MAX) {
		fprintf(stderr, "-maxdepth %d ", cmdline->maxdepth);
	}
	if (cmdline->stdout_colors) {
		fputs("-color ", stderr);
	} else {
		fputs("-nocolor ", stderr);
	}

	dump_expr(cmdline->expr);

	fputs("\n", stderr);
}

/**
 * Parse the command line.
 */
struct cmdline *parse_cmdline(int argc, char *argv[]) {
	struct cmdline *cmdline = malloc(sizeof(struct cmdline));
	if (!cmdline) {
		goto fail;
	}

	cmdline->roots = NULL;
	cmdline->nroots = 0;
	cmdline->mindepth = 0;
	cmdline->maxdepth = INT_MAX;
	cmdline->flags = BFTW_RECOVER;
	cmdline->optlevel = 3;
	cmdline->debug = 0;
	cmdline->expr = &expr_true;
	cmdline->nopen_files = 0;

	cmdline->colors = parse_colors(getenv("LS_COLORS"));
	cmdline->stdout_colors = isatty(STDOUT_FILENO) ? cmdline->colors : NULL;
	cmdline->stderr_colors = isatty(STDERR_FILENO) ? cmdline->colors : NULL;

	struct parser_state state = {
		.cmdline = cmdline,
		.argv = argv + 1,
		.command = argv[0],
		.implicit_print = true,
		.warn = true,
		.non_option_seen = false,
		.just_info = false,
	};

	if (clock_gettime(CLOCK_REALTIME, &state.now) != 0) {
		perror("clock_gettime()");
		goto fail;
	}

	if (skip_paths(&state)) {
		cmdline->expr = parse_expr(&state);
		if (!cmdline->expr) {
			if (state.just_info) {
				goto done;
			} else {
				goto fail;
			}
		}
	}

	if (state.argv[0]) {
		pretty_error(cmdline->stderr_colors,
		             "error: Unexpected argument '%s'.\n", state.argv[0]);
		goto fail;
	}

	if (state.implicit_print) {
		struct expr *print = new_expr(eval_print, false, 1, &fake_print_arg);
		if (!print) {
			goto fail;
		}

		cmdline->expr = new_and_expr(&state, cmdline->expr, print, &fake_and_arg);
		if (!cmdline->expr) {
			goto fail;
		}
	}

	cmdline->expr = optimize_whole_expr(&state, cmdline->expr);

	if (cmdline->nroots == 0) {
		if (!cmdline_add_root(cmdline, ".")) {
			goto fail;
		}
	}

	if (cmdline->debug & DEBUG_TREE) {
		dump_cmdline(cmdline);
	}

done:
	return cmdline;

fail:
	free_cmdline(cmdline);
	return NULL;
}
