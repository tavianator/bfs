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
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

struct eval_state {
	/** Data about the current file. */
	struct BFTW *ftwbuf;
	/** The parsed command line. */
	const struct cmdline *cl;
	/** The bftw() callback return value. */
	enum bftw_action action;
	/** A stat() buffer, if necessary. */
	struct stat statbuf;
};

/**
 * Perform a stat() call if necessary.
 */
static const struct stat *fill_statbuf(struct eval_state *state) {
	struct BFTW *ftwbuf = state->ftwbuf;
	if (!ftwbuf->statbuf) {
		if (fstatat(ftwbuf->at_fd, ftwbuf->at_path, &state->statbuf, AT_SYMLINK_NOFOLLOW) == 0) {
			ftwbuf->statbuf = &state->statbuf;
		} else {
			perror("fstatat()");
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
	return faccessat(ftwbuf->at_fd, ftwbuf->at_path, expr->idata, AT_SYMLINK_NOFOLLOW) == 0;
}

/**
 * -[acm]{min,time} tests.
 */
bool eval_acmtime(const struct expr *expr, struct eval_state *state) {
	const struct stat *statbuf = fill_statbuf(state);
	if (!statbuf) {
		return false;
	}

	const struct timespec *time;
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

	const struct timespec *time;
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

	return time->tv_sec > expr->reftime.tv_sec
		|| (time->tv_sec == expr->reftime.tv_sec && time->tv_nsec > expr->reftime.tv_nsec);
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
		print_error(state->cl->colors, ftwbuf->path, errno);
		state->action = BFTW_STOP;
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
			perror("openat()");
			goto done;
		}

		DIR *dir = fdopendir(dfd);
		if (!dir) {
			perror("fdopendir()");
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
 * -name test.
 */
bool eval_name(const struct expr *expr, struct eval_state *state) {
	struct BFTW *ftwbuf = state->ftwbuf;
	return fnmatch(expr->sdata, ftwbuf->path + ftwbuf->nameoff, 0) == 0;
}

/**
 * -path test.
 */
bool eval_path(const struct expr *expr, struct eval_state *state) {
	struct BFTW *ftwbuf = state->ftwbuf;
	return fnmatch(expr->sdata, ftwbuf->path, 0) == 0;
}

/**
 * -print action.
 */
bool eval_print(const struct expr *expr, struct eval_state *state) {
	struct color_table *colors = state->cl->colors;
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
 * -type test.
 */
bool eval_type(const struct expr *expr, struct eval_state *state) {
	return state->ftwbuf->typeflag == expr->idata;
}

/**
 * Evaluate a negation.
 */
bool eval_not(const struct expr *expr, struct eval_state *state) {
	return !expr->rhs->eval(expr, state);
}

/**
 * Evaluate a conjunction.
 */
bool eval_and(const struct expr *expr, struct eval_state *state) {
	return expr->lhs->eval(expr->lhs, state) && expr->rhs->eval(expr->rhs, state);
}

/**
 * Evaluate a disjunction.
 */
bool eval_or(const struct expr *expr, struct eval_state *state) {
	return expr->lhs->eval(expr->lhs, state) || expr->rhs->eval(expr->rhs, state);
}

/**
 * Evaluate the comma operator.
 */
bool eval_comma(const struct expr *expr, struct eval_state *state) {
	expr->lhs->eval(expr->lhs, state);
	return expr->rhs->eval(expr->rhs, state);
}

/**
 * Infer the number of open file descriptors we're allowed to have.
 */
static int infer_nopenfd() {
	int ret = 4096;

	struct rlimit rl;
	if (getrlimit(RLIMIT_NOFILE, &rl) == 0) {
		if (rl.rlim_cur != RLIM_INFINITY) {
			ret = rl.rlim_cur;
		}
	}

	// Account for std{in,out,err}
	if (ret > 3) {
		ret -= 3;
	}

	return ret;
}

/**
 * bftw() callback.
 */
static enum bftw_action cmdline_callback(struct BFTW *ftwbuf, void *ptr) {
	const struct cmdline *cl = ptr;

	if (ftwbuf->typeflag == BFTW_ERROR) {
		print_error(cl->colors, ftwbuf->path, ftwbuf->error);
		return BFTW_SKIP_SUBTREE;
	}

	struct eval_state state = {
		.ftwbuf = ftwbuf,
		.cl = cl,
		.action = BFTW_CONTINUE,
	};

	if (ftwbuf->depth >= cl->maxdepth) {
		state.action = BFTW_SKIP_SUBTREE;
	}

	// In -depth mode, only handle directories on the BFTW_POST visit
	enum bftw_visit expected_visit = BFTW_PRE;
	if ((cl->flags & BFTW_DEPTH)
	    && ftwbuf->typeflag == BFTW_DIR
	    && ftwbuf->depth < cl->maxdepth) {
		expected_visit = BFTW_POST;
	}

	if (ftwbuf->visit == expected_visit
	    && ftwbuf->depth >= cl->mindepth
	    && ftwbuf->depth <= cl->maxdepth) {
		cl->expr->eval(cl->expr, &state);
	}

	return state.action;
}

/**
 * Evaluate the command line.
 */
int eval_cmdline(struct cmdline *cl) {
	int ret = 0;
	int nopenfd = infer_nopenfd();

	for (size_t i = 0; i < cl->nroots; ++i) {
		if (bftw(cl->roots[i], cmdline_callback, nopenfd, cl->flags, cl) != 0) {
			ret = -1;
			perror("bftw()");
		}
	}

	return ret;
}
