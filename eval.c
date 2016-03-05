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
#include "bftw.h"
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

struct eval_state {
	/** Data about the current file. */
	struct BFTW *ftwbuf;
	/** The parsed command line. */
	const struct cmdline *cmdline;
	/** The bftw() callback return value. */
	enum bftw_action action;
	/** The eval_cmdline() return value. */
	int ret;
	/** A stat() buffer, if necessary. */
	struct stat statbuf;
};

/**
 * Report an error that occurs during evaluation.
 */
static void eval_error(struct eval_state *state) {
	pretty_error(state->cmdline->stderr_colors,
	             "'%s': %s\n", state->ftwbuf->path, strerror(errno));
	state->ret = -1;
}

/**
 * Perform a stat() call if necessary.
 */
static const struct stat *fill_statbuf(struct eval_state *state) {
	struct BFTW *ftwbuf = state->ftwbuf;
	if (!ftwbuf->statbuf) {
		if (fstatat(ftwbuf->at_fd, ftwbuf->at_path, &state->statbuf, ftwbuf->at_flags) == 0) {
			ftwbuf->statbuf = &state->statbuf;
		} else {
			eval_error(state);
		}
	}
	return ftwbuf->statbuf;
}

/**
 * Get the difference (in seconds) between two struct timespecs.
 */
static time_t timespec_diff(const struct timespec *lhs, const struct timespec *rhs) {
	time_t ret = lhs->tv_sec - rhs->tv_sec;
	if (lhs->tv_nsec < rhs->tv_nsec) {
		--ret;
	}
	return ret;
}

/**
 * Perform a comparison.
 */
static bool do_cmp(const struct expr *expr, int n) {
	switch (expr->cmp) {
	case CMP_EXACT:
		return n == expr->idata;
	case CMP_LESS:
		return n < expr->idata;
	case CMP_GREATER:
		return n > expr->idata;
	}

	return false;
}

/**
 * -true test.
 */
bool eval_true(const struct expr *expr, struct eval_state *state) {
	return true;
}

/**
 * -false test.
 */
bool eval_false(const struct expr *expr, struct eval_state *state) {
	return false;
}

/**
 * -executable, -readable, -writable tests.
 */
bool eval_access(const struct expr *expr, struct eval_state *state) {
	struct BFTW *ftwbuf = state->ftwbuf;
	return faccessat(ftwbuf->at_fd, ftwbuf->at_path, expr->idata, ftwbuf->at_flags) == 0;
}

/**
 * -[acm]{min,time} tests.
 */
bool eval_acmtime(const struct expr *expr, struct eval_state *state) {
	const struct stat *statbuf = fill_statbuf(state);
	if (!statbuf) {
		return false;
	}

	const struct timespec *time = NULL;
	switch (expr->timefield) {
	case ATIME:
		time = &statbuf->st_atim;
		break;
	case CTIME:
		time = &statbuf->st_ctim;
		break;
	case MTIME:
		time = &statbuf->st_mtim;
		break;
	}
	assert(time);

	time_t diff = timespec_diff(&expr->reftime, time);
	switch (expr->timeunit) {
	case MINUTES:
		diff /= 60;
		break;
	case DAYS:
		diff /= 60*60*24;
		break;
	}

	return do_cmp(expr, diff);
}

/**
 * -[ac]?newer tests.
 */
bool eval_acnewer(const struct expr *expr, struct eval_state *state) {
	const struct stat *statbuf = fill_statbuf(state);
	if (!statbuf) {
		return false;
	}

	const struct timespec *time = NULL;
	switch (expr->timefield) {
	case ATIME:
		time = &statbuf->st_atim;
		break;
	case CTIME:
		time = &statbuf->st_ctim;
		break;
	case MTIME:
		time = &statbuf->st_mtim;
		break;
	}
	assert(time);

	return time->tv_sec > expr->reftime.tv_sec
		|| (time->tv_sec == expr->reftime.tv_sec && time->tv_nsec > expr->reftime.tv_nsec);
}

/**
 * -used test.
 */
bool eval_used(const struct expr *expr, struct eval_state *state) {
	const struct stat *statbuf = fill_statbuf(state);
	if (!statbuf) {
		return false;
	}

	time_t diff = timespec_diff(&statbuf->st_atim, &statbuf->st_ctim);
	diff /= 60*60*24;
	printf("%d\n", (int)diff);
	return do_cmp(expr, diff);
}

/**
 * -gid test.
 */
bool eval_gid(const struct expr *expr, struct eval_state *state) {
	const struct stat *statbuf = fill_statbuf(state);
	if (!statbuf) {
		return false;
	}

	return do_cmp(expr, statbuf->st_gid);
}

/**
 * -uid test.
 */
bool eval_uid(const struct expr *expr, struct eval_state *state) {
	const struct stat *statbuf = fill_statbuf(state);
	if (!statbuf) {
		return false;
	}

	return do_cmp(expr, statbuf->st_uid);
}

/**
 * -delete action.
 */
bool eval_delete(const struct expr *expr, struct eval_state *state) {
	struct BFTW *ftwbuf = state->ftwbuf;

	int flag = 0;
	if (ftwbuf->typeflag == BFTW_DIR) {
		flag |= AT_REMOVEDIR;
	}

	if (unlinkat(ftwbuf->at_fd, ftwbuf->at_path, flag) != 0) {
		eval_error(state);
		return false;
	}

	return true;
}

/**
 * -empty test.
 */
bool eval_empty(const struct expr *expr, struct eval_state *state) {
	bool ret = false;
	struct BFTW *ftwbuf = state->ftwbuf;

	if (ftwbuf->typeflag == BFTW_DIR) {
		int dfd = openat(ftwbuf->at_fd, ftwbuf->at_path, O_DIRECTORY);
		if (dfd < 0) {
			eval_error(state);
			goto done;
		}

		DIR *dir = fdopendir(dfd);
		if (!dir) {
			eval_error(state);
			close(dfd);
			goto done;
		}

		ret = true;

		struct dirent *de;
		while ((de = readdir(dir)) != NULL) {
			if (strcmp(de->d_name, ".") != 0 && strcmp(de->d_name, "..") != 0) {
				ret = false;
				break;
			}
		}

		closedir(dir);
	} else {
		const struct stat *statbuf = fill_statbuf(state);
		if (statbuf) {
			ret = statbuf->st_size == 0;
		}
	}

done:
	return ret;
}

/**
 * -prune action.
 */
bool eval_prune(const struct expr *expr, struct eval_state *state) {
	state->action = BFTW_SKIP_SUBTREE;
	return true;
}

/**
 * -hidden test.
 */
bool eval_hidden(const struct expr *expr, struct eval_state *state) {
	struct BFTW *ftwbuf = state->ftwbuf;
	return ftwbuf->nameoff > 0 && ftwbuf->path[ftwbuf->nameoff] == '.';
}

/**
 * -nohidden action.
 */
bool eval_nohidden(const struct expr *expr, struct eval_state *state) {
	if (eval_hidden(expr, state)) {
		eval_prune(expr, state);
		return false;
	} else {
		return true;
	}
}

/**
 * -inum test.
 */
bool eval_inum(const struct expr *expr, struct eval_state *state) {
	const struct stat *statbuf = fill_statbuf(state);
	if (!statbuf) {
		return false;
	}

	return do_cmp(expr, statbuf->st_ino);
}

/**
 * -links test.
 */
bool eval_links(const struct expr *expr, struct eval_state *state) {
	const struct stat *statbuf = fill_statbuf(state);
	if (!statbuf) {
		return false;
	}

	return do_cmp(expr, statbuf->st_nlink);
}

/**
 * -i?lname test.
 */
bool eval_lname(const struct expr *expr, struct eval_state *state) {
	bool ret = false;
	char *name = NULL;

	struct BFTW *ftwbuf = state->ftwbuf;
	if (ftwbuf->typeflag != BFTW_LNK) {
		goto done;
	}

	const struct stat *statbuf = fill_statbuf(state);
	if (!statbuf) {
		goto done;
	}

	size_t size = statbuf->st_size + 1;
	name = malloc(size);
	if (!name) {
		eval_error(state);
		goto done;
	}

	ssize_t len = readlinkat(ftwbuf->at_fd, ftwbuf->at_path, name, size);
	if (len < 0) {
		eval_error(state);
		goto done;
	} else if (len >= size) {
		goto done;
	}

	name[len] = '\0';

	ret = fnmatch(expr->sdata, name, expr->idata) == 0;

done:
	free(name);
	return ret;
}

/**
 * -i?name test.
 */
bool eval_name(const struct expr *expr, struct eval_state *state) {
	struct BFTW *ftwbuf = state->ftwbuf;

	const char *name = ftwbuf->path + ftwbuf->nameoff;
	char *copy = NULL;
	if (ftwbuf->depth == 0) {
		// Any trailing slashes are not part of the name.  This can only
		// happen for the root path.
		const char *slash = strchr(name, '/');
		if (slash == name) {
			// The name of "/" (or "//", etc.) is "/"
			name = "/";
		} else if (slash) {
			copy = strdup(name);
			if (!copy) {
				eval_error(state);
				return false;
			}
			copy[slash - name] = '\0';
			name = copy;
		}
	}

	bool ret = fnmatch(expr->sdata, name, expr->idata) == 0;
	free(copy);
	return ret;
}

/**
 * -i?path test.
 */
bool eval_path(const struct expr *expr, struct eval_state *state) {
	struct BFTW *ftwbuf = state->ftwbuf;
	return fnmatch(expr->sdata, ftwbuf->path, expr->idata) == 0;
}

/**
 * -print action.
 */
bool eval_print(const struct expr *expr, struct eval_state *state) {
	const struct colors *colors = state->cmdline->stdout_colors;
	if (colors) {
		fill_statbuf(state);
	}

	pretty_print(colors, state->ftwbuf);
	return true;
}

/**
 * -print0 action.
 */
bool eval_print0(const struct expr *expr, struct eval_state *state) {
	const char *path = state->ftwbuf->path;
	fwrite(path, 1, strlen(path) + 1, stdout);
	return true;
}

/**
 * -quit action.
 */
bool eval_quit(const struct expr *expr, struct eval_state *state) {
	state->action = BFTW_STOP;
	return true;
}

/**
 * -samefile test.
 */
bool eval_samefile(const struct expr *expr, struct eval_state *state) {
	const struct stat *statbuf = fill_statbuf(state);
	if (!statbuf) {
		return false;
	}

	return statbuf->st_dev == expr->dev && statbuf->st_ino == expr->ino;
}

/**
 * -type test.
 */
bool eval_type(const struct expr *expr, struct eval_state *state) {
	return state->ftwbuf->typeflag == expr->idata;
}

/**
 * -xtype test.
 */
bool eval_xtype(const struct expr *expr, struct eval_state *state) {
	struct BFTW *ftwbuf = state->ftwbuf;

	bool is_root = ftwbuf->depth == 0;
	bool follow = state->cmdline->flags & (is_root ? BFTW_FOLLOW_ROOT : BFTW_FOLLOW_NONROOT);

	bool is_link = ftwbuf->typeflag == BFTW_LNK;
	if (follow == is_link) {
		return eval_type(expr, state);
	}

	// -xtype does the opposite of everything else
	int at_flags = follow ? AT_SYMLINK_NOFOLLOW : 0;

	struct stat sb;
	if (fstatat(ftwbuf->at_fd, ftwbuf->at_path, &sb, at_flags) != 0) {
		if (!follow && errno == ENOENT) {
			// Broken symlink
			return eval_type(expr, state);
		} else {
			eval_error(state);
			return false;
		}
	}

	switch (expr->idata) {
	case BFTW_BLK:
		return S_ISBLK(sb.st_mode);
	case BFTW_CHR:
		return S_ISCHR(sb.st_mode);
	case BFTW_DIR:
		return S_ISDIR(sb.st_mode);
	case BFTW_FIFO:
		return S_ISFIFO(sb.st_mode);
	case BFTW_LNK:
		return S_ISLNK(sb.st_mode);
	case BFTW_REG:
		return S_ISREG(sb.st_mode);
	case BFTW_SOCK:
		return S_ISSOCK(sb.st_mode);
	}

	return false;
}

/**
 * Evaluate an expression.
 */
static bool eval_expr(const struct expr *expr, struct eval_state *state) {
	return expr->eval(expr, state);
}

/**
 * Evaluate a negation.
 */
bool eval_not(const struct expr *expr, struct eval_state *state) {
	return !eval_expr(expr->rhs, state);
}

/**
 * Evaluate a conjunction.
 */
bool eval_and(const struct expr *expr, struct eval_state *state) {
	return eval_expr(expr->lhs, state) && eval_expr(expr->rhs, state);
}

/**
 * Evaluate a disjunction.
 */
bool eval_or(const struct expr *expr, struct eval_state *state) {
	return eval_expr(expr->lhs, state) || eval_expr(expr->rhs, state);
}

/**
 * Evaluate the comma operator.
 */
bool eval_comma(const struct expr *expr, struct eval_state *state) {
	eval_expr(expr->lhs, state);
	return eval_expr(expr->rhs, state);
}

/**
 * Debug stat() calls.
 */
void debug_stat(const struct eval_state *state) {
	struct BFTW *ftwbuf = state->ftwbuf;

	fprintf(stderr, "fstatat(");
	if (ftwbuf->at_fd == AT_FDCWD) {
		fprintf(stderr, "AT_FDCWD");
	} else {
		size_t baselen = strlen(ftwbuf->path) - strlen(ftwbuf->at_path);
		fprintf(stderr, "\"");
		fwrite(ftwbuf->path, 1, baselen, stderr);
		fprintf(stderr, "\"");
	}

	fprintf(stderr, ", \"%s\", ", ftwbuf->at_path);

	if (ftwbuf->at_flags == AT_SYMLINK_NOFOLLOW) {
		fprintf(stderr, "AT_SYMLINK_NOFOLLOW");
	} else {
		fprintf(stderr, "%d", ftwbuf->at_flags);
	}

	fprintf(stderr, ")\n");
}

/**
 * Type passed as the argument to the bftw() callback.
 */
struct callback_args {
	/** The parsed command line. */
	const struct cmdline *cmdline;
	/** Eventual return value from eval_cmdline(). */
	int ret;
	/** The last error code seen. */
	int last_error;
};

/**
 * bftw() callback.
 */
static enum bftw_action cmdline_callback(struct BFTW *ftwbuf, void *ptr) {
	struct callback_args *args = ptr;

	const struct cmdline *cmdline = args->cmdline;

	if (ftwbuf->typeflag == BFTW_ERROR) {
		args->last_error = ftwbuf->error;
		pretty_error(cmdline->stderr_colors, "'%s': %s\n", ftwbuf->path, strerror(ftwbuf->error));
		return BFTW_SKIP_SUBTREE;
	}

	struct eval_state state = {
		.ftwbuf = ftwbuf,
		.cmdline = cmdline,
		.action = BFTW_CONTINUE,
		.ret = args->ret,
	};

	if (ftwbuf->depth >= cmdline->maxdepth) {
		state.action = BFTW_SKIP_SUBTREE;
	}

	// In -depth mode, only handle directories on the BFTW_POST visit
	enum bftw_visit expected_visit = BFTW_PRE;
	if ((cmdline->flags & BFTW_DEPTH)
	    && ftwbuf->typeflag == BFTW_DIR
	    && ftwbuf->depth < cmdline->maxdepth) {
		expected_visit = BFTW_POST;
	}

	if (ftwbuf->visit == expected_visit
	    && ftwbuf->depth >= cmdline->mindepth
	    && ftwbuf->depth <= cmdline->maxdepth) {
		eval_expr(cmdline->expr, &state);
	}

	if ((cmdline->debug & DEBUG_STAT) && ftwbuf->statbuf) {
		debug_stat(&state);
	}

	args->ret = state.ret;
	return state.action;
}

/**
 * Infer the number of open file descriptors we're allowed to have.
 */
static int infer_fdlimit() {
	int ret = 4096;

	struct rlimit rl;
	if (getrlimit(RLIMIT_NOFILE, &rl) == 0) {
		if (rl.rlim_cur != RLIM_INFINITY) {
			ret = rl.rlim_cur;
		}
	}

	// Account for std{in,out,err}, and allow an extra fd for the predicates
	int nopen = 4;

	// Check /dev/fd for the current number of open fds, if possible (we may
	// have inherited more than just the standard ones)
	DIR *dir = opendir("/dev/fd");
	if (dir) {
		nopen = 0;

		struct dirent *de;
		while ((de = readdir(dir)) != NULL) {
			if (strcmp(de->d_name, ".") != 0 && strcmp(de->d_name, "..") != 0) {
				++nopen;
			}
		}

		closedir(dir);
	}

	if (ret > nopen) {
		ret -= nopen;
	} else {
		ret = 1;
	}

	return ret;
}

/**
 * Evaluate the command line.
 */
int eval_cmdline(const struct cmdline *cmdline) {
	if (!cmdline->expr) {
		return 0;
	}

	if (cmdline->optlevel >= 3 && cmdline->expr->eval == eval_false) {
		return 0;
	}

	int nopenfd = infer_fdlimit();

	struct callback_args args = {
		.cmdline = cmdline,
		.ret = 0,
	};

	for (size_t i = 0; i < cmdline->nroots; ++i) {
		args.last_error = 0;

		if (bftw(cmdline->roots[i], cmdline_callback, nopenfd, cmdline->flags, &args) != 0) {
			args.ret = -1;

			if (errno != args.last_error) {
				perror("bftw()");
			}
		}
	}

	return args.ret;
}
