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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/**
 * Singleton true expression instance.
 */
static struct expr expr_true = {
	.eval = eval_true,
	.lhs = NULL,
	.rhs = NULL,
	.pure = true,
};

/**
 * Singleton false expression instance.
 */
static struct expr expr_false = {
	.eval = eval_false,
	.lhs = NULL,
	.rhs = NULL,
	.pure = true,
};

/**
 * Free an expression.
 */
static void free_expr(struct expr *expr) {
	if (expr && expr != &expr_true && expr != &expr_false) {
		free_expr(expr->lhs);
		free_expr(expr->rhs);
		free(expr);
	}
}

/**
 * Create a new expression.
 */
static struct expr *new_expr(eval_fn *eval, struct expr *lhs, struct expr *rhs, bool pure) {
	struct expr *expr = malloc(sizeof(struct expr));
	if (!expr) {
		perror("malloc()");
		free_expr(rhs);
		free_expr(lhs);
		return NULL;
	}

	expr->eval = eval;
	expr->lhs = lhs;
	expr->rhs = rhs;
	expr->pure = pure;
	return expr;
}

/**
 * Create a new unary expression.
 */
static struct expr *new_unary_expr(eval_fn *eval, struct expr *rhs) {
	return new_expr(eval, NULL, rhs, rhs->pure);
}

/**
 * Create a new binary expression.
 */
static struct expr *new_binary_expr(eval_fn *eval, struct expr *lhs, struct expr *rhs) {
	return new_expr(eval, lhs, rhs, lhs->pure && rhs->pure);
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
	/** The command line being parsed. */
	struct cmdline *cmdline;
	/** The command line arguments. */
	char **argv;
	/** Current argument index. */
	int i;

	/** Whether a -print action is implied. */
	bool implicit_print;
	/** Whether warnings are enabled (see -warn, -nowarn). */
	bool warn;
	/** Whether any non-option arguments have been encountered. */
	bool non_option_seen;
	/** Whether an information option like -help or -version was passed. */
	bool just_info;

	/** The current time. */
	struct timespec now;
};

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
		             "'%s': %s\n", expr->sdata, strerror(errno));
		free_expr(expr);
	}
	return ret;
}

/**
 * Parse the expression specified on the command line.
 */
static struct expr *parse_expr(struct parser_state *state);

/**
 * While parsing an expression, skip any paths and add them to the cmdline.
 */
static const char *skip_paths(struct parser_state *state) {
	while (true) {
		const char *arg = state->argv[state->i];
		if (!arg
		    || arg[0] == '-'
		    || strcmp(arg, "(") == 0
		    || strcmp(arg, ")") == 0
		    || strcmp(arg, "!") == 0
		    || strcmp(arg, ",") == 0) {
			return arg;
		}

		if (!cmdline_add_root(state->cmdline, arg)) {
			return NULL;
		}

		++state->i;
	}
}

/**
 * Parse an integer.
 */
static bool parse_int(const struct parser_state *state, const char *str, int *value) {
	char *endptr;
	long result = strtol(str, &endptr, 10);

	if (*str == '\0' || *endptr != '\0') {
		goto bad;
	}

	if (result < INT_MIN || result > INT_MAX) {
		goto bad;
	}

	*value = result;
	return true;

bad:
	pretty_error(state->cmdline->stderr_colors,
	             "error: '%s' is not a valid integer.\n", str);
	return false;
}

/**
 * Parse an integer and a comparison flag.
 */
static bool parse_icmp(const struct parser_state *state, const char *str, struct expr *expr) {
	switch (str[0]) {
	case '-':
		expr->cmp = CMP_LESS;
		++str;
		break;
	case '+':
		expr->cmp = CMP_GREATER;
		++str;
		break;
	default:
		expr->cmp = CMP_EXACT;
		break;
	}

	return parse_int(state, str, &expr->idata);
}

/**
 * Create a new option expression.
 */
static struct expr *new_option(struct parser_state *state, const char *option) {
	if (state->warn && state->non_option_seen) {
		pretty_warning(state->cmdline->stderr_colors,
		               "warning: The '%s' option applies to the entire command line.  For clarity, place\n"
		               "it before any non-option arguments.\n\n",
		               option);
	}

	return &expr_true;
}

/**
 * Create a new positional option expression.
 */
static struct expr *new_positional_option(struct parser_state *state) {
	return &expr_true;
}

/**
 * Create a new test expression.
 */
static struct expr *new_test(struct parser_state *state, eval_fn *eval) {
	state->non_option_seen = true;
	return new_expr(eval, NULL, NULL, true);
}

/**
 * Create a new test expression with integer data.
 */
static struct expr *new_test_idata(struct parser_state *state, eval_fn *eval, int idata) {
	struct expr *test = new_test(state, eval);
	if (test) {
		test->idata = idata;
	}
	return test;
}

/**
 * Create a new test expression with string data.
 */
static struct expr *new_test_sdata(struct parser_state *state, eval_fn *eval, const char *sdata) {
	struct expr *test = new_test(state, eval);
	if (test) {
		test->sdata = sdata;
	}
	return test;
}

/**
 * Create a new action expression.
 */
static struct expr *new_action(struct parser_state *state, eval_fn *eval) {
	if (eval != eval_nohidden && eval != eval_prune) {
		state->implicit_print = false;
	}

	state->non_option_seen = true;

	return new_expr(eval, NULL, NULL, false);
}

/**
 * Parse a test expression with integer data and a comparison flag.
 */
static struct expr *parse_test_icmp(struct parser_state *state, const char *test, eval_fn *eval) {
	const char *arg = state->argv[state->i];
	if (!arg) {
		pretty_error(state->cmdline->stderr_colors,
		             "error: %s needs a value.\n", test);
		return NULL;
	}

	++state->i;

	struct expr *expr = new_test(state, eval);
	if (expr) {
		if (!parse_icmp(state, arg, expr)) {
			free_expr(expr);
			expr = NULL;
		}
	}
	return expr;
}

/**
 * Parse a test that takes a string argument.
 */
static struct expr *parse_test_sdata(struct parser_state *state, const char *test, eval_fn *eval) {
	const char *arg = state->argv[state->i];
	if (!arg) {
		pretty_error(state->cmdline->stderr_colors,
		             "error: %s needs a value.\n", test);
		return NULL;
	}

	++state->i;

	return new_test_sdata(state, eval, arg);
}

/**
 * Parse -[acm]{min,time}.
 */
static struct expr *parse_acmtime(struct parser_state *state, const char *option, enum timefield field, enum timeunit unit) {
	struct expr *expr = parse_test_icmp(state, option, eval_acmtime);
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
static struct expr *parse_acnewer(struct parser_state *state, const char *option, enum timefield field) {
	struct expr *expr = parse_test_sdata(state, option, eval_acnewer);
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

	tm.tm_sec = 0;
	tm.tm_min = 0;
	tm.tm_hour = 0;
	++tm.tm_mday;
	time_t time = mktime(&tm);
	if (time == -1) {
		perror("mktime()");
		return NULL;
	}

	state->now.tv_sec = time;
	state->now.tv_nsec = 0;

	return new_positional_option(state);
}

/**
 * Parse -{min,max}depth N.
 */
static struct expr *parse_depth(struct parser_state *state, const char *option, int *depth) {
	const char *arg = state->argv[state->i];
	if (!arg) {
		pretty_error(state->cmdline->stderr_colors,
		             "error: %s needs a value.\n", option);
		return NULL;
	}

	++state->i;

	if (!parse_int(state, arg, depth)) {
		return NULL;
	}

	return new_option(state, option);
}

/**
 * Parse -group.
 */
static struct expr *parse_group(struct parser_state *state, const char *option) {
	struct expr *expr = parse_test_sdata(state, option, eval_gid);
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
		if (!parse_int(state, expr->sdata, &expr->idata)) {
			goto fail;
		}
	} else {
		error = "No such group";
		goto error;
	}

	expr->cmp = CMP_EXACT;
	return expr;

error:
	pretty_error(state->cmdline->stderr_colors,
	             "error: %s %s: %s\n", option, expr->sdata, error);

fail:
	free_expr(expr);
	return NULL;
}

/**
 * Parse -user.
 */
static struct expr *parse_user(struct parser_state *state, const char *option) {
	struct expr *expr = parse_test_sdata(state, option, eval_uid);
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
		if (!parse_int(state, expr->sdata, &expr->idata)) {
			goto fail;
		}
	} else {
		error = "No such user";
		goto error;
	}

	expr->cmp = CMP_EXACT;
	return expr;

error:
	pretty_error(state->cmdline->stderr_colors,
	             "error: %s %s: %s\n", option, expr->sdata, error);

fail:
	free_expr(expr);
	return NULL;
}

/**
 * Set the FNM_CASEFOLD flag, if supported.
 */
static struct expr *set_fnm_casefold(const struct parser_state *state, const char *option, struct expr *expr, bool casefold) {
	if (expr) {
		if (casefold) {
#ifdef FNM_CASEFOLD
			expr->idata = FNM_CASEFOLD;
#else
			pretty_error(state->cmdline->stderr_colors,
			             "error: %s is missing platform support.\n", option);
			free(expr);
			expr = NULL;
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
static struct expr *parse_name(struct parser_state *state, const char *option, bool casefold) {
	struct expr *expr = parse_test_sdata(state, option, eval_name);
	return set_fnm_casefold(state, option, expr, casefold);
}

/**
 * Parse -i?path, -i?wholename.
 */
static struct expr *parse_path(struct parser_state *state, const char *option, bool casefold) {
	struct expr *expr = parse_test_sdata(state, option, eval_path);
	return set_fnm_casefold(state, option, expr, casefold);
}

/**
 * Parse -i?lname.
 */
static struct expr *parse_lname(struct parser_state *state, const char *option, bool casefold) {
	struct expr *expr = parse_test_sdata(state, option, eval_lname);
	return set_fnm_casefold(state, option, expr, casefold);
}

/**
 * Parse -noleaf.
 */
static struct expr *parse_noleaf(struct parser_state *state, const char *option) {
	if (state->warn) {
		pretty_warning(state->cmdline->stderr_colors,
		               "warning: bfs does not apply the optimization that %s inhibits.\n\n",
		               option);
	}

	return new_option(state, option);
}

/**
 * Parse -samefile FILE.
 */
static struct expr *parse_samefile(struct parser_state *state, const char *option) {
	struct expr *expr = parse_test_sdata(state, option, eval_samefile);
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
 * Parse -x?type [bcdpfls].
 */
static struct expr *parse_type(struct parser_state *state, const char *option, eval_fn *eval) {
	const char *arg = state->argv[state->i];
	if (!arg) {
		pretty_error(state->cmdline->stderr_colors,
		             "error: %s needs a value.\n", option);
		return NULL;
	}

	int typeflag = BFTW_UNKNOWN;

	switch (arg[0]) {
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

	if (typeflag == BFTW_UNKNOWN || arg[1] != '\0') {
		pretty_error(state->cmdline->stderr_colors,
		             "error: Unknown type flag '%s'.\n", arg);
		return NULL;
	}

	++state->i;

	return new_test_idata(state, eval, typeflag);
}

/**
 * "Parse" -help.
 */
static struct expr *parse_help(struct parser_state *state) {
	printf("Usage: %s [arguments...]\n\n", state->argv[0]);

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
	const char *arg = state->argv[state->i++];

	if (arg[0] != '-') {
		pretty_error(cmdline->stderr_colors,
		             "error: Expected a predicate; found '%s'.\n", arg);
		return NULL;
	}

	switch (arg[1]) {
	case 'P':
		if (strcmp(arg, "-P") == 0) {
			cmdline->flags &= ~(BFTW_FOLLOW | BFTW_DETECT_CYCLES);
			return new_positional_option(state);
		}
		break;

	case 'H':
		if (strcmp(arg, "-H") == 0) {
			cmdline->flags &= ~(BFTW_FOLLOW_NONROOT | BFTW_DETECT_CYCLES);
			cmdline->flags |= BFTW_FOLLOW_ROOT;
			return new_positional_option(state);
		}
		break;

	case 'L':
		if (strcmp(arg, "-L") == 0) {
			cmdline->flags |= BFTW_FOLLOW | BFTW_DETECT_CYCLES;
			return new_positional_option(state);
		}
		break;

	case 'a':
		if (strcmp(arg, "-amin") == 0) {
			return parse_acmtime(state, arg, ATIME, MINUTES);
		} else if (strcmp(arg, "-atime") == 0) {
			return parse_acmtime(state, arg, ATIME, DAYS);
		} else if (strcmp(arg, "-anewer") == 0) {
			return parse_acnewer(state, arg, ATIME);
		}
		break;

	case 'c':
		if (strcmp(arg, "-cmin") == 0) {
			return parse_acmtime(state, arg, CTIME, MINUTES);
		} else if (strcmp(arg, "-ctime") == 0) {
			return parse_acmtime(state, arg, CTIME, DAYS);
		} else if (strcmp(arg, "-cnewer") == 0) {
			return parse_acnewer(state, arg, CTIME);
		} else if (strcmp(arg, "-color") == 0) {
			cmdline->stdout_colors = cmdline->colors;
			cmdline->stderr_colors = cmdline->colors;
			return new_option(state, arg);
		}
		break;

	case 'd':
		if (strcmp(arg, "-daystart") == 0) {
			return parse_daystart(state);
		} else if (strcmp(arg, "-delete") == 0) {
			cmdline->flags |= BFTW_DEPTH;
			return new_action(state, eval_delete);
		} else if (strcmp(arg, "-d") == 0 || strcmp(arg, "-depth") == 0) {
			cmdline->flags |= BFTW_DEPTH;
			return new_option(state, arg);
		}
		break;

	case 'e':
		if (strcmp(arg, "-empty") == 0) {
			return new_test(state, eval_empty);
		} else if (strcmp(arg, "-executable") == 0) {
			return new_test_idata(state, eval_access, X_OK);
		}
		break;

	case 'f':
		if (strcmp(arg, "-false") == 0) {
			return &expr_false;
		} else if (strcmp(arg, "-follow") == 0) {
			cmdline->flags |= BFTW_FOLLOW | BFTW_DETECT_CYCLES;
			return new_positional_option(state);
		}
		break;

	case 'g':
		if (strcmp(arg, "-gid") == 0) {
			return parse_test_icmp(state, arg, eval_gid);
		} else if (strcmp(arg, "-group") == 0) {
			return parse_group(state, arg);
		}
		break;

	case 'h':
		if (strcmp(arg, "-help") == 0) {
			return parse_help(state);
		} else if (strcmp(arg, "-hidden") == 0) {
			return new_test(state, eval_hidden);
		}
		break;

	case 'i':
		if (strcmp(arg, "-ilname") == 0) {
			return parse_lname(state, arg, true);
		} if (strcmp(arg, "-iname") == 0) {
			return parse_name(state, arg, true);
		} else if (strcmp(arg, "-inum") == 0) {
			return parse_test_icmp(state, arg, eval_inum);
		} else if (strcmp(arg, "-ipath") == 0 || strcmp(arg, "-iwholename") == 0) {
			return parse_path(state, arg, true);
		}
		break;

	case 'l':
		if (strcmp(arg, "-links") == 0) {
			return parse_test_icmp(state, arg, eval_links);
		} else if (strcmp(arg, "-lname") == 0) {
			return parse_lname(state, arg, false);
		}
		break;

	case 'm':
		if (strcmp(arg, "-mindepth") == 0) {
			return parse_depth(state, arg, &cmdline->mindepth);
		} else if (strcmp(arg, "-maxdepth") == 0) {
			return parse_depth(state, arg, &cmdline->maxdepth);
		} else if (strcmp(arg, "-mmin") == 0) {
			return parse_acmtime(state, arg, MTIME, MINUTES);
		} else if (strcmp(arg, "-mount") == 0) {
			cmdline->flags |= BFTW_MOUNT;
			return new_option(state, arg);
		} else if (strcmp(arg, "-mtime") == 0) {
			return parse_acmtime(state, arg, MTIME, DAYS);
		}
		break;

	case 'n':
		if (strcmp(arg, "-name") == 0) {
			return parse_name(state, arg, false);
		} else if (strcmp(arg, "-newer") == 0) {
			return parse_acnewer(state, arg, MTIME);
		} else if (strcmp(arg, "-nocolor") == 0) {
			cmdline->stdout_colors = NULL;
			cmdline->stderr_colors = NULL;
			return new_option(state, arg);
		} else if (strcmp(arg, "-nohidden") == 0) {
			return new_action(state, eval_nohidden);
		} else if (strcmp(arg, "-noleaf") == 0) {
			return parse_noleaf(state, arg);
		} else if (strcmp(arg, "-nowarn") == 0) {
			state->warn = false;
			return new_positional_option(state);
		}
		break;

	case 'p':
		if (strcmp(arg, "-path") == 0) {
			return parse_path(state, arg, false);
		} else if (strcmp(arg, "-print") == 0) {
			return new_action(state, eval_print);
		} else if (strcmp(arg, "-print0") == 0) {
			return new_action(state, eval_print0);
		} else if (strcmp(arg, "-prune") == 0) {
			return new_action(state, eval_prune);
		}
		break;

	case 'q':
		if (strcmp(arg, "-quit") == 0) {
			return new_action(state, eval_quit);
		}
		break;

	case 'r':
		if (strcmp(arg, "-readable") == 0) {
			return new_test_idata(state, eval_access, R_OK);
		}
		break;

	case 's':
		if (strcmp(arg, "-samefile") == 0) {
			return parse_samefile(state, arg);
		}
		break;

	case 't':
		if (strcmp(arg, "-true") == 0) {
			return &expr_true;
		} else if (strcmp(arg, "-type") == 0) {
			return parse_type(state, arg, eval_type);
		}
		break;

	case  'u':
		if (strcmp(arg, "-uid") == 0) {
			return parse_test_icmp(state, arg, eval_uid);
		} else if (strcmp(arg, "-user") == 0) {
			return parse_user(state, arg);
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
			return new_positional_option(state);
		} else if (strcmp(arg, "-wholename") == 0) {
			return parse_path(state, arg, false);
		} else if (strcmp(arg, "-writable") == 0) {
			return new_test_idata(state, eval_access, W_OK);
		}
		break;

	case 'x':
		if (strcmp(arg, "-xdev") == 0) {
			cmdline->flags |= BFTW_MOUNT;
			return new_option(state, arg);
		} else if (strcmp(arg, "-xtype") == 0) {
			return parse_type(state, arg, eval_xtype);
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

/**
 * Create a "not" expression.
 */
static struct expr *new_not_expr(struct expr *rhs) {
	if (rhs == &expr_true) {
		return &expr_false;
	} else if (rhs == &expr_false) {
		return &expr_true;
	} else if (rhs->eval == eval_not) {
		struct expr *expr = rhs->rhs;
		rhs->rhs = NULL;
		free_expr(rhs);
		return expr;
	} else {
		return new_unary_expr(eval_not, rhs);
	}
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
		++state->i;
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
		++state->i;

		return expr;
	} else if (strcmp(arg, "!") == 0 || strcmp(arg, "-not") == 0) {
		++state->i;

		struct expr *factor = parse_factor(state);
		if (!factor) {
			return NULL;
		}

		return new_not_expr(factor);
	} else {
		return parse_literal(state);
	}
}

/**
 * Create an "and" expression.
 */
static struct expr *new_and_expr(struct expr *lhs, struct expr *rhs) {
	if (lhs == &expr_true) {
		return rhs;
	} else if (lhs == &expr_false) {
		free_expr(rhs);
		return lhs;
	} else if (rhs == &expr_true) {
		return lhs;
	} else if (rhs == &expr_false && lhs->pure) {
		free_expr(lhs);
		return rhs;
	} else {
		return new_binary_expr(eval_and, lhs, rhs);
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
		const char *arg = skip_paths(state);
		if (!arg) {
			break;
		}

		if (strcmp(arg, "-o") == 0 || strcmp(arg, "-or") == 0
		    || strcmp(arg, ",") == 0
		    || strcmp(arg, ")") == 0) {
			break;
		}

		if (strcmp(arg, "-a") == 0 || strcmp(arg, "-and") == 0) {
			++state->i;
		}

		struct expr *lhs = term;
		struct expr *rhs = parse_factor(state);
		if (!rhs) {
			free_expr(lhs);
			return NULL;
		}

		term = new_and_expr(lhs, rhs);
	}

	return term;
}

/**
 * Create an "or" expression.
 */
static struct expr *new_or_expr(struct expr *lhs, struct expr *rhs) {
	if (lhs == &expr_true) {
		free_expr(rhs);
		return lhs;
	} else if (lhs == &expr_false) {
		return rhs;
	} else if (rhs == &expr_true && lhs->pure) {
		free_expr(lhs);
		return rhs;
	} else if (rhs == &expr_false) {
		return lhs;
	} else {
		return new_binary_expr(eval_or, lhs, rhs);
	}
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

		++state->i;

		struct expr *lhs = clause;
		struct expr *rhs = parse_term(state);
		if (!rhs) {
			free_expr(lhs);
			return NULL;
		}

		clause = new_or_expr(lhs, rhs);
	}

	return clause;
}

/**
 * Create a "comma" expression.
 */
static struct expr *new_comma_expr(struct expr *lhs, struct expr *rhs) {
	if (lhs->pure) {
		free_expr(lhs);
		return rhs;
	} else {
		return new_binary_expr(eval_comma, lhs, rhs);
	}
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

		++state->i;

		struct expr *lhs = expr;
		struct expr *rhs = parse_clause(state);
		if (!rhs) {
			free_expr(lhs);
			return NULL;
		}

		expr = new_comma_expr(lhs, rhs);
	}

	return expr;
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
	cmdline->expr = &expr_true;

	cmdline->colors = parse_colors(getenv("LS_COLORS"));
	cmdline->stdout_colors = isatty(STDOUT_FILENO) ? cmdline->colors : NULL;
	cmdline->stderr_colors = isatty(STDERR_FILENO) ? cmdline->colors : NULL;

	struct parser_state state = {
		.cmdline = cmdline,
		.argv = argv,
		.i = 1,
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

	if (state.i < argc) {
		pretty_error(cmdline->stderr_colors,
		             "error: Unexpected argument '%s'.\n", argv[state.i]);
		goto fail;
	}

	if (state.implicit_print) {
		struct expr *print = new_action(&state, eval_print);
		if (!print) {
			goto fail;
		}

		cmdline->expr = new_and_expr(cmdline->expr, print);
		if (!cmdline->expr) {
			goto fail;
		}
	}

	if (cmdline->nroots == 0) {
		if (!cmdline_add_root(cmdline, ".")) {
			goto fail;
		}
	}

done:
	return cmdline;

fail:
	free_cmdline(cmdline);
	return NULL;
}
