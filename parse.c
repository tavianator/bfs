#include "bfs.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/**
 * Create a new expression.
 */
static expression *new_expression(eval_fn *eval) {
	expression *expr = malloc(sizeof(expression));
	if (!expr) {
		perror("malloc()");
		return NULL;
	}

	expr->lhs = NULL;
	expr->rhs = NULL;
	expr->eval = eval;
	expr->cmp = CMP_EXACT;
	expr->idata = 0;
	expr->sdata = NULL;
	return expr;
}

/**
 * Singleton true expression instance.
 */
static expression expr_true = {
	.lhs = NULL,
	.rhs = NULL,
	.eval = eval_true,
	.idata = 0,
	.sdata = NULL,
};

/**
 * Singleton false expression instance.
 */
static expression expr_false = {
	.lhs = NULL,
	.rhs = NULL,
	.eval = eval_false,
	.idata = 0,
	.sdata = NULL,
};

/**
 * Free an expression.
 */
static void free_expression(expression *expr) {
	if (expr && expr != &expr_true && expr != &expr_false) {
		free_expression(expr->lhs);
		free_expression(expr->rhs);
		free(expr);
	}
}

/**
 * Create a new unary expression.
 */
static expression *new_unary_expression(expression *rhs, eval_fn *eval) {
	expression *expr = new_expression(eval);
	if (!expr) {
		free_expression(rhs);
		return NULL;
	}

	expr->rhs = rhs;
	expr->eval = eval;
	return expr;
}

/**
 * Create a new binary expression.
 */
static expression *new_binary_expression(expression *lhs, expression *rhs, eval_fn *eval) {
	expression *expr = new_expression(eval);
	if (!expr) {
		free_expression(rhs);
		free_expression(lhs);
		return NULL;
	}

	expr->lhs = lhs;
	expr->rhs = rhs;
	expr->eval = eval;
	return expr;
}

/**
 * Free the parsed command line.
 */
void free_cmdline(cmdline *cl) {
	if (cl) {
		free_expression(cl->expr);
		free_colors(cl->colors);
		free(cl->roots);
		free(cl);
	}
}

/**
 * Add a root path to the cmdline.
 */
static bool cmdline_add_root(cmdline *cl, const char *root) {
	size_t i = cl->nroots++;
	const char **roots = realloc(cl->roots, cl->nroots*sizeof(const char *));
	if (!roots) {
		perror("realloc()");
		return false;
	}

	roots[i] = root;
	cl->roots = roots;
	return true;
}

/**
 * Ephemeral state for parsing the command line.
 */
typedef struct {
	/** The command line being parsed. */
	cmdline *cl;
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
} parser_state;

/**
 * Parse the expression specified on the command line.
 */
static expression *parse_expression(parser_state *state);

/**
 * While parsing an expression, skip any paths and add them to the cmdline.
 */
static const char *skip_paths(parser_state *state) {
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

		if (!cmdline_add_root(state->cl, arg)) {
			return NULL;
		}

		++state->i;
	}
}

/**
 * Parse an integer.
 */
static bool parse_int(const char *str, int *value) {
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
	fprintf(stderr, "'%s' is not a valid integer.\n", str);
	return false;
}

/**
 * Parse an integer and a comparison flag.
 */
static bool parse_icmp(const char *str, expression *expr) {
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

	return parse_int(str, &expr->idata);
}

/**
 * Create a new option expression.
 */
static expression *new_option(parser_state *state, const char *option) {
	if (state->warn && state->non_option_seen) {
		fprintf(stderr,
		        "The '%s' option applies to the entire command line.\n"
		        "For clarity, place it before any non-option arguments.\n\n",
		        option);
	}

	return &expr_true;
}

/**
 * Create a new positional option expression.
 */
static expression *new_positional_option(parser_state *state) {
	return &expr_true;
}

/**
 * Create a new test expression.
 */
static expression *new_test(parser_state *state, eval_fn *eval) {
	state->non_option_seen = true;
	return new_expression(eval);
}

/**
 * Create a new test expression with integer data.
 */
static expression *new_test_idata(parser_state *state, eval_fn *eval, int idata) {
	expression *test = new_test(state, eval);
	if (test) {
		test->idata = idata;
	}
	return test;
}

/**
 * Create a new test expression with string data.
 */
static expression *new_test_sdata(parser_state *state, eval_fn *eval, const char *sdata) {
	expression *test = new_test(state, eval);
	if (test) {
		test->sdata = sdata;
	}
	return test;
}

/**
 * Create a new action expression.
 */
static expression *new_action(parser_state *state, eval_fn *eval) {
	if (eval != eval_nohidden && eval != eval_prune) {
		state->implicit_print = false;
	}

	state->non_option_seen = true;

	return new_expression(eval);
}

/**
 * Parse a test expression with integer data and a comparison flag.
 */
static expression *parse_test_icmp(parser_state *state, const char *test, eval_fn *eval) {
	const char *arg = state->argv[state->i];
	if (!arg) {
		fprintf(stderr, "%s needs a value.\n", test);
		return NULL;
	}

	++state->i;

	expression *expr = new_test(state, eval);
	if (expr) {
		if (!parse_icmp(arg, expr)) {
			free_expression(expr);
			expr = NULL;
		}
	}
	return expr;
}

/**
 * Parse a test that takes a string argument.
 */
static expression *parse_test_sdata(parser_state *state, const char *test, eval_fn *eval) {
	const char *arg = state->argv[state->i];
	if (!arg) {
		fprintf(stderr, "%s needs a value.\n", test);
		return NULL;
	}

	++state->i;

	return new_test_sdata(state, eval, arg);
}

/**
 * Parse -{min,max}depth N.
 */
static expression *parse_depth(parser_state *state, const char *option, int *depth) {
	const char *arg = state->argv[state->i];
	if (!arg) {
		fprintf(stderr, "%s needs a value.\n", option);
		return NULL;
	}

	++state->i;

	if (!parse_int(arg, depth)) {
		return NULL;
	}

	return new_option(state, option);
}

/**
 * Parse -type [bcdpfls].
 */
static expression *parse_type(parser_state *state) {
	const char *arg = state->argv[state->i];
	if (!arg) {
		fputs("-type needs a value.\n", stderr);
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
		fprintf(stderr, "Unknown type flag '%s'.\n", arg);
		return NULL;
	}

	++state->i;

	return new_test_idata(state, eval_type, typeflag);
}

/**
 * LITERAL : OPTION
 *         | TEST
 *         | ACTION
 */
static expression *parse_literal(parser_state *state) {
	// Paths are already skipped at this point
	const char *arg = state->argv[state->i++];

	if (strcmp(arg, "-amin") == 0) {
		return parse_test_icmp(state, arg, eval_amin);
	} else if (strcmp(arg, "-atime") == 0) {
		return parse_test_icmp(state, arg, eval_atime);
	} else if (strcmp(arg, "-cmin") == 0) {
		return parse_test_icmp(state, arg, eval_cmin);
	} else if (strcmp(arg, "-ctime") == 0) {
		return parse_test_icmp(state, arg, eval_ctime);
	} else if (strcmp(arg, "-color") == 0) {
		state->cl->color = true;
		return new_option(state, arg);
	} else if (strcmp(arg, "-nocolor") == 0) {
		state->cl->color = false;
		return new_option(state, arg);
	} else if (strcmp(arg, "-delete") == 0) {
		state->cl->flags |= BFTW_DEPTH;
		return new_action(state, eval_delete);
	} else if (strcmp(arg, "-d") == 0 || strcmp(arg, "-depth") == 0) {
		state->cl->flags |= BFTW_DEPTH;
		return new_option(state, arg);
	} else if (strcmp(arg, "-empty") == 0) {
		return new_test(state, eval_empty);
	} else if (strcmp(arg, "-executable") == 0) {
		return new_test_idata(state, eval_access, X_OK);
	} else if (strcmp(arg, "-false") == 0) {
		return &expr_false;
	} else if (strcmp(arg, "-hidden") == 0) {
		return new_test(state, eval_hidden);
	} else if (strcmp(arg, "-nohidden") == 0) {
		return new_action(state, eval_nohidden);
	} else if (strcmp(arg, "-mindepth") == 0) {
		return parse_depth(state, arg, &state->cl->mindepth);
	} else if (strcmp(arg, "-maxdepth") == 0) {
		return parse_depth(state, arg, &state->cl->maxdepth);
	} else if (strcmp(arg, "-mmin") == 0) {
		return parse_test_icmp(state, arg, eval_mmin);
	} else if (strcmp(arg, "-mtime") == 0) {
		return parse_test_icmp(state, arg, eval_mtime);
	} else if (strcmp(arg, "-name") == 0) {
		return parse_test_sdata(state, arg, eval_name);
	} else if (strcmp(arg, "-path") == 0 || strcmp(arg, "-wholename") == 0) {
		return parse_test_sdata(state, arg, eval_path);
	} else if (strcmp(arg, "-print") == 0) {
		return new_action(state, eval_print);
	} else if (strcmp(arg, "-print0") == 0) {
		return new_action(state, eval_print0);
	} else if (strcmp(arg, "-prune") == 0) {
		return new_action(state, eval_prune);
	} else if (strcmp(arg, "-quit") == 0) {
		return new_action(state, eval_quit);
	} else if (strcmp(arg, "-readable") == 0) {
		return new_test_idata(state, eval_access, R_OK);
	} else if (strcmp(arg, "-true") == 0) {
		return &expr_true;
	} else if (strcmp(arg, "-type") == 0) {
		return parse_type(state);
	} else if (strcmp(arg, "-warn") == 0) {
		state->warn = true;
		return new_positional_option(state);
	} else if (strcmp(arg, "-nowarn") == 0) {
		state->warn = false;
		return new_positional_option(state);
	} else if (strcmp(arg, "-writable") == 0) {
		return new_test_idata(state, eval_access, W_OK);
	} else {
		fprintf(stderr, "Unknown argument '%s'.\n", arg);
		return NULL;
	}
}

/**
 * Create a "not" expression.
 */
static expression *new_not_expression(expression *rhs) {
	if (rhs == &expr_true) {
		return &expr_false;
	} else if (rhs == &expr_false) {
		return &expr_true;
	} else {
		return new_unary_expression(rhs, eval_not);
	}
}

/**
 * FACTOR : "(" EXPR ")"
 *        | "!" FACTOR | "-not" FACTOR
 *        | LITERAL
 */
static expression *parse_factor(parser_state *state) {
	const char *arg = skip_paths(state);
	if (!arg) {
		fputs("Expression terminated prematurely.\n", stderr);
		return NULL;
	}

	if (strcmp(arg, "(") == 0) {
		++state->i;
		expression *expr = parse_expression(state);
		if (!expr) {
			return NULL;
		}

		arg = skip_paths(state);
		if (!arg || strcmp(arg, ")") != 0) {
			fputs("Expected a ')'.\n", stderr);
			free_expression(expr);
			return NULL;
		}
		++state->i;

		return expr;
	} else if (strcmp(arg, "!") == 0 || strcmp(arg, "-not") == 0) {
		++state->i;

		expression *factor = parse_factor(state);
		if (!factor) {
			return NULL;
		}

		return new_not_expression(factor);
	} else {
		return parse_literal(state);
	}
}

/**
 * Create an "and" expression.
 */
static expression *new_and_expression(expression *lhs, expression *rhs) {
	if (lhs == &expr_true) {
		return rhs;
	} else if (lhs == &expr_false) {
		free_expression(rhs);
		return lhs;
	} else if (rhs == &expr_true) {
		return lhs;
	} else {
		return new_binary_expression(lhs, rhs, eval_and);
	}
}

/**
 * TERM : FACTOR
 *      | TERM FACTOR
 *      | TERM "-a" FACTOR
 *      | TERM "-and" FACTOR
 */
static expression *parse_term(parser_state *state) {
	expression *term = parse_factor(state);

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

		expression *lhs = term;
		expression *rhs = parse_factor(state);
		if (!rhs) {
			free_expression(lhs);
			return NULL;
		}

		term = new_and_expression(lhs, rhs);
	}

	return term;
}

/**
 * Create an "or" expression.
 */
static expression *new_or_expression(expression *lhs, expression *rhs) {
	if (lhs == &expr_true) {
		free_expression(rhs);
		return lhs;
	} else if (lhs == &expr_false) {
		return rhs;
	} else if (rhs == &expr_false) {
		return lhs;
	} else {
		return new_binary_expression(lhs, rhs, eval_or);
	}
}

/**
 * CLAUSE : TERM
 *        | CLAUSE "-o" TERM
 *        | CLAUSE "-or" TERM
 */
static expression *parse_clause(parser_state *state) {
	expression *clause = parse_term(state);

	while (clause) {
		const char *arg = skip_paths(state);
		if (!arg) {
			break;
		}

		if (strcmp(arg, "-o") != 0 && strcmp(arg, "-or") != 0) {
			break;
		}

		++state->i;

		expression *lhs = clause;
		expression *rhs = parse_term(state);
		if (!rhs) {
			free_expression(lhs);
			return NULL;
		}

		clause = new_or_expression(lhs, rhs);
	}

	return clause;
}

/**
 * Create a "comma" expression.
 */
static expression *new_comma_expression(expression *lhs, expression *rhs) {
	if (lhs == &expr_true || lhs == &expr_false) {
		return rhs;
	} else {
		return new_binary_expression(lhs, rhs, eval_comma);
	}
}

/**
 * EXPR : CLAUSE
 *      | EXPR "," CLAUSE
 */
static expression *parse_expression(parser_state *state) {
	expression *expr = parse_clause(state);

	while (expr) {
		const char *arg = skip_paths(state);
		if (!arg) {
			break;
		}

		if (strcmp(arg, ",") != 0) {
			break;
		}

		++state->i;

		expression *lhs = expr;
		expression *rhs = parse_clause(state);
		if (!rhs) {
			free_expression(lhs);
			return NULL;
		}

		expr = new_comma_expression(lhs, rhs);
	}

	return expr;
}

/**
 * Parse the command line.
 */
cmdline *parse_cmdline(int argc, char *argv[]) {
	cmdline *cl = malloc(sizeof(cmdline));
	if (!cl) {
		goto fail;
	}

	cl->roots = NULL;
	cl->nroots = 0;
	cl->colors = NULL;
	cl->color = isatty(STDOUT_FILENO);
	cl->mindepth = 0;
	cl->maxdepth = INT_MAX;
	cl->flags = BFTW_RECOVER;
	cl->expr = &expr_true;

	if (clock_gettime(CLOCK_REALTIME, &cl->now) != 0) {
		perror("clock_gettime()");
		goto fail;
	}

	parser_state state = {
		.cl = cl,
		.argv = argv,
		.i = 1,
		.implicit_print = true,
		.warn = true,
		.non_option_seen = false,
	};

	if (skip_paths(&state)) {
		cl->expr = parse_expression(&state);
		if (!cl->expr) {
			goto fail;
		}
	}

	if (state.i < argc) {
		fprintf(stderr, "Unexpected argument '%s'.\n", argv[state.i]);
		goto fail;
	}

	if (state.implicit_print) {
		expression *print = new_expression(eval_print);
		if (!print) {
			goto fail;
		}

		cl->expr = new_and_expression(cl->expr, print);
		if (!cl->expr) {
			goto fail;
		}
	}

	if (cl->nroots == 0) {
		if (!cmdline_add_root(cl, ".")) {
			goto fail;
		}
	}

	if (cl->color) {
		cl->colors = parse_colors(getenv("LS_COLORS"));
	}

	return cl;

fail:
	free_cmdline(cl);
	return NULL;
}
