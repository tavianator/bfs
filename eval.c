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
#include "dstring.h"
#include "util.h"
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
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
	int *ret;
	/** Whether to quit immediately. */
	bool *quit;
	/** A stat() buffer, if necessary. */
	struct stat statbuf;
};

/**
 * Check if an error should be ignored.
 */
static bool eval_should_ignore(const struct eval_state *state, int error) {
	return state->cmdline->ignore_races
		&& error == ENOENT
		&& state->ftwbuf->depth > 0;
}

/**
 * Report an error that occurs during evaluation.
 */
static void eval_error(struct eval_state *state) {
	if (!eval_should_ignore(state, errno)) {
		pretty_error(state->cmdline->stderr_colors,
		             "'%s': %s\n", state->ftwbuf->path, strerror(errno));
		*state->ret = -1;
	}
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
static bool do_cmp(const struct expr *expr, long long n) {
	switch (expr->cmp_flag) {
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
	return faccessat(ftwbuf->at_fd, ftwbuf->at_path, expr->idata, 0) == 0;
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
	switch (expr->time_field) {
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
	switch (expr->time_unit) {
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
	switch (expr->time_field) {
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
 * -nogroup test.
 */
bool eval_nogroup(const struct expr *expr, struct eval_state *state) {
	const struct stat *statbuf = fill_statbuf(state);
	if (!statbuf) {
		return false;
	}

	return getgrgid(statbuf->st_gid) == NULL;
}

/**
 * -nouser test.
 */
bool eval_nouser(const struct expr *expr, struct eval_state *state) {
	const struct stat *statbuf = fill_statbuf(state);
	if (!statbuf) {
		return false;
	}

	return getpwuid(statbuf->st_uid) == NULL;
}

/**
 * -delete action.
 */
bool eval_delete(const struct expr *expr, struct eval_state *state) {
	struct BFTW *ftwbuf = state->ftwbuf;

	// Don't try to delete the current directory
	if (strcmp(ftwbuf->path, ".") == 0) {
		return true;
	}

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

static const char *exec_format_path(const struct expr *expr, const struct BFTW *ftwbuf) {
	if (!(expr->exec_flags & EXEC_CHDIR)) {
		return ftwbuf->path;
	}

	const char *name = ftwbuf->path + ftwbuf->nameoff;

	if (name[0] == '/') {
		// Must be a root path ("/", "//", etc.)
		return name;
	}

	// For compatibility with GNU find, use './name' instead of just 'name'
	char *path = dstralloc(2 + strlen(name));
	if (!path) {
		return NULL;
	}

	if (dstrcat(&path, "./") != 0) {
		goto err;
	}
	if (dstrcat(&path, name) != 0) {
		goto err;
	}

	return path;

err:
	dstrfree(path);
	return NULL;
}

static void exec_free_path(const char *path, const struct BFTW *ftwbuf) {
	if (path != ftwbuf->path && path != ftwbuf->path + ftwbuf->nameoff) {
		dstrfree((char *)path);
	}
}

static char *exec_format_arg(char *arg, const char *path) {
	char *match = strstr(arg, "{}");
	if (!match) {
		return arg;
	}

	char *ret = dstralloc(0);
	if (!ret) {
		return NULL;
	}

	char *last = arg;
	do {
		if (dstrncat(&ret, last, match - last) != 0) {
			goto err;
		}
		if (dstrcat(&ret, path) != 0) {
			goto err;
		}

		last = match + 2;
		match = strstr(last, "{}");
	} while (match);

	if (dstrcat(&ret, last) != 0) {
		goto err;
	}

	return ret;

err:
	dstrfree(ret);
	return NULL;
}

static void exec_free_argv(size_t argc, char **argv, char **template) {
	for (size_t i = 0; i < argc; ++i) {
		if (argv[i] != template[i]) {
			dstrfree(argv[i]);
		}
	}
	free(argv);
}

static char **exec_format_argv(size_t argc, char **template, const char *path) {
	char **argv = malloc((argc + 1)*sizeof(char *));
	if (!argv) {
		return NULL;
	}

	for (size_t i = 0; i < argc; ++i) {
		argv[i] = exec_format_arg(template[i], path);
		if (!argv[i]) {
			exec_free_argv(i, argv, template);
			return NULL;
		}
	}
	argv[argc] = NULL;

	return argv;
}

static void exec_chdir(const struct BFTW *ftwbuf) {
	if (ftwbuf->at_fd != AT_FDCWD) {
		if (fchdir(ftwbuf->at_fd) != 0) {
			perror("fchdir()");
			_Exit(EXIT_FAILURE);
		}
		return;
	}

	size_t nameoff = ftwbuf->nameoff;

	if (nameoff == 0 && ftwbuf->path[nameoff] != '/') {
		// The path is something like "foo", so we're already in the
		// right directory
		return;
	}

	char *path = strdup(ftwbuf->path);
	if (!path) {
		perror("strdup()");
		_Exit(EXIT_FAILURE);
	}

	if (nameoff > 0) {
		path[nameoff] = '\0';
	}

	if (chdir(path) != 0) {
		perror("chdir()");
		_Exit(EXIT_FAILURE);
	}
}

/**
 * -exec[dir]/-ok[dir] actions.
 */
bool eval_exec(const struct expr *expr, struct eval_state *state) {
	bool ret = false;

	const struct BFTW *ftwbuf = state->ftwbuf;

	const char *path = exec_format_path(expr, ftwbuf);
	if (!path) {
		eval_error(state);
		goto out;
	}

	size_t argc = expr->argc - 2;
	char **template = expr->argv + 1;
	char **argv = exec_format_argv(argc, template, path);
	if (!argv) {
		eval_error(state);
		goto out_path;
	}

	if (expr->exec_flags & EXEC_CONFIRM) {
		for (size_t i = 0; i < argc; ++i) {
			fprintf(stderr, "%s ", argv[i]);
		}
		fprintf(stderr, "? ");
		fflush(stderr);

		int c = getchar();
		bool exec = c == 'y' || c == 'Y';
		while (c != '\n' && c != EOF) {
			c = getchar();
		}
		if (!exec) {
			goto out_argv;
		}
	}

	pid_t pid = fork();

	if (pid < 0) {
		eval_error(state);
		goto out_argv;
	} else if (pid > 0) {
		int status;
		if (waitpid(pid, &status, 0) < 0) {
			eval_error(state);
			goto out_argv;
		}

		ret = WIFEXITED(status) && WEXITSTATUS(status) == EXIT_SUCCESS;
	} else {
		if (expr->exec_flags & EXEC_CHDIR) {
			exec_chdir(ftwbuf);
		}

		execvp(argv[0], argv);
		perror("execvp()");
		_Exit(EXIT_FAILURE);
	}

out_argv:
	exec_free_argv(argc, argv, template);
out_path:
	exec_free_path(path, ftwbuf);
out:
	return ret;
}

/**
 * -depth N test.
 */
bool eval_depth(const struct expr *expr, struct eval_state *state) {
	return do_cmp(expr, state->ftwbuf->depth);
}

/**
 * -empty test.
 */
bool eval_empty(const struct expr *expr, struct eval_state *state) {
	bool ret = false;
	struct BFTW *ftwbuf = state->ftwbuf;

	if (ftwbuf->typeflag == BFTW_DIR) {
		int flags = 0;
#ifdef O_DIRECTORY
		flags |= O_DIRECTORY;
#endif
		int dfd = openat(ftwbuf->at_fd, ftwbuf->at_path, flags);
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

		while (true) {
			struct dirent *de;
			if (xreaddir(dir, &de) != 0) {
				eval_error(state);
				goto done_dir;
			}
			if (!de) {
				break;
			}

			if (strcmp(de->d_name, ".") != 0 && strcmp(de->d_name, "..") != 0) {
				ret = false;
				break;
			}
		}

	done_dir:
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

	name = xreadlinkat(ftwbuf->at_fd, ftwbuf->at_path, statbuf->st_size);
	if (!name) {
		eval_error(state);
		goto done;
	}

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
		if (slash && slash > name) {
			copy = strndup(name, slash - name);
			if (!copy) {
				eval_error(state);
				return false;
			}
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
 * -perm test.
 */
bool eval_perm(const struct expr *expr, struct eval_state *state) {
	const struct stat *statbuf = fill_statbuf(state);
	if (!statbuf) {
		return false;
	}

	mode_t mode = statbuf->st_mode;
	mode_t target;
	if (state->ftwbuf->typeflag == BFTW_DIR) {
		target = expr->dir_mode;
	} else {
		target = expr->file_mode;
	}

	switch (expr->mode_cmp) {
	case MODE_EXACT:
		return (mode & 07777) == target;

	case MODE_ALL:
		return (mode & target) == target;

	case MODE_ANY:
		return !(mode & target) == !target;
	}

	return false;
}

/**
 * -fprint action.
 */
bool eval_fprint(const struct expr *expr, struct eval_state *state) {
	const char *path = state->ftwbuf->path;
	if (fputs(path, expr->file) == EOF) {
		goto error;
	}
	if (fputc('\n', expr->file) == EOF) {
		goto error;
	}
	return true;

error:
	eval_error(state);
	return true;
}

/**
 * -f?print0 action.
 */
bool eval_fprint0(const struct expr *expr, struct eval_state *state) {
	const char *path = state->ftwbuf->path;
	size_t length = strlen(path) + 1;
	if (fwrite(path, 1, length, expr->file) != length) {
		eval_error(state);
	}
	return true;
}

/**
 * -f?printf action.
 */
bool eval_fprintf(const struct expr *expr, struct eval_state *state) {
	if (expr->printf->needs_stat) {
		if (!fill_statbuf(state)) {
			goto done;
		}
	}

	if (bfs_printf(expr->file, expr->printf, state->ftwbuf) != 0) {
		eval_error(state);
	}

done:
	return true;
}

/**
 * -print action.
 */
bool eval_print(const struct expr *expr, struct eval_state *state) {
	const struct colors *colors = state->cmdline->stdout_colors;
	if (colors) {
		fill_statbuf(state);
	}

	if (pretty_print(colors, state->ftwbuf) != 0) {
		eval_error(state);
	}

	return true;
}

/**
 * -prune action.
 */
bool eval_prune(const struct expr *expr, struct eval_state *state) {
	state->action = BFTW_SKIP_SUBTREE;
	return true;
}

/**
 * -quit action.
 */
bool eval_quit(const struct expr *expr, struct eval_state *state) {
	state->action = BFTW_STOP;
	*state->quit = true;
	return true;
}

/**
 * -i?regex test.
 */
bool eval_regex(const struct expr *expr, struct eval_state *state) {
	const char *path = state->ftwbuf->path;
	size_t len = strlen(path);
	regmatch_t match = {
		.rm_so = 0,
		.rm_eo = len,
	};

	int flags = 0;
#ifdef REG_STARTEND
	flags |= REG_STARTEND;
#endif
	int err = regexec(expr->regex, path, 1, &match, flags);
	if (err == 0) {
		return match.rm_so == 0 && match.rm_eo == len;
	} else if (err != REG_NOMATCH) {
		char *str = xregerror(err, expr->regex);
		if (str) {
			pretty_error(state->cmdline->stderr_colors,
			             "'%s': %s\n", path, str);
			free(str);
		} else {
			perror("xregerror()");
		}

		*state->ret = -1;
	}

	return false;
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
 * -size test.
 */
bool eval_size(const struct expr *expr, struct eval_state *state) {
	const struct stat *statbuf = fill_statbuf(state);
	if (!statbuf) {
		return false;
	}

	static off_t scales[] = {
		[SIZE_BLOCKS] = 512,
		[SIZE_BYTES] = 1,
		[SIZE_WORDS] = 2,
		[SIZE_KB] = 1024,
		[SIZE_MB] = 1024LL*1024,
		[SIZE_GB] = 1024LL*1024*1024,
		[SIZE_TB] = 1024LL*1024*1024*1024,
		[SIZE_PB] = 1024LL*1024*1024*1024*1024,
	};

	off_t scale = scales[expr->size_unit];
	off_t size = (statbuf->st_size + scale - 1)/scale; // Round up
	return do_cmp(expr, size);
}

/**
 * -sparse test.
 */
bool eval_sparse(const struct expr *expr, struct eval_state *state) {
	const struct stat *statbuf = fill_statbuf(state);
	if (!statbuf) {
		return false;
	}

	blkcnt_t expected = (statbuf->st_size + 511)/512;
	return statbuf->st_blocks < expected;
}

/**
 * -type test.
 */
bool eval_type(const struct expr *expr, struct eval_state *state) {
	return state->ftwbuf->typeflag & expr->idata;
}

/**
 * -xtype test.
 */
bool eval_xtype(const struct expr *expr, struct eval_state *state) {
	struct BFTW *ftwbuf = state->ftwbuf;

	int follow_flags = BFTW_LOGICAL;
	if (ftwbuf->depth == 0) {
		follow_flags |= BFTW_COMFOLLOW;
	}
	bool follow = state->cmdline->flags & follow_flags;

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

	return bftw_mode_to_typeflag(sb.st_mode) & expr->idata;
}

#if _POSIX_MONOTONIC_CLOCK > 0
#	define BFS_CLOCK CLOCK_MONOTONIC
#elif _POSIX_TIMERS > 0
#	define BFS_CLOCK CLOCK_REALTIME
#endif

/**
 * Call clock_gettime(), if available.
 */
static int eval_gettime(struct timespec *ts) {
#ifdef BFS_CLOCK
	int ret = clock_gettime(BFS_CLOCK, ts);
	if (ret != 0) {
		perror("clock_gettime()");
	}
	return ret;
#else
	return -1;
#endif
}

/**
 * Record the time that elapsed evaluating an expression.
 */
static void add_elapsed(struct expr *expr, const struct timespec *start, const struct timespec *end) {
	expr->elapsed.tv_sec += end->tv_sec - start->tv_sec;
	expr->elapsed.tv_nsec += end->tv_nsec - start->tv_nsec;
	if (expr->elapsed.tv_nsec < 0) {
		expr->elapsed.tv_nsec += 1000000000L;
		--expr->elapsed.tv_sec;
	} else if (expr->elapsed.tv_nsec >= 1000000000L) {
		expr->elapsed.tv_nsec -= 1000000000L;
		++expr->elapsed.tv_sec;
	}
}

/**
 * Evaluate an expression.
 */
static bool eval_expr(struct expr *expr, struct eval_state *state) {
	struct timespec start, end;
	bool time = state->cmdline->debug & DEBUG_RATES;
	if (time) {
		if (eval_gettime(&start) != 0) {
			time = false;
		}
	}

	bool ret = expr->eval(expr, state);

	if (time) {
		if (eval_gettime(&end) == 0) {
			add_elapsed(expr, &start, &end);
		}
	}

	++expr->evaluations;
	if (ret) {
		++expr->successes;
	}

	return ret;
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
	if (!eval_expr(expr->lhs, state)) {
		return false;
	}

	if (*state->quit) {
		return false;
	}

	return eval_expr(expr->rhs, state);
}

/**
 * Evaluate a disjunction.
 */
bool eval_or(const struct expr *expr, struct eval_state *state) {
	if (eval_expr(expr->lhs, state)) {
		return true;
	}

	if (*state->quit) {
		return false;
	}

	return eval_expr(expr->rhs, state);
}

/**
 * Evaluate the comma operator.
 */
bool eval_comma(const struct expr *expr, struct eval_state *state) {
	eval_expr(expr->lhs, state);

	if (*state->quit) {
		return false;
	}

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
	/** Whether to quit immediately. */
	bool quit;
};

/**
 * bftw() callback.
 */
static enum bftw_action cmdline_callback(struct BFTW *ftwbuf, void *ptr) {
	struct callback_args *args = ptr;

	const struct cmdline *cmdline = args->cmdline;

	struct eval_state state = {
		.ftwbuf = ftwbuf,
		.cmdline = cmdline,
		.action = BFTW_CONTINUE,
		.ret = &args->ret,
		.quit = &args->quit,
	};

	if (ftwbuf->typeflag == BFTW_ERROR) {
		if (!eval_should_ignore(&state, ftwbuf->error)) {
			args->ret = -1;
			pretty_error(cmdline->stderr_colors, "'%s': %s\n", ftwbuf->path, strerror(ftwbuf->error));
		}
		state.action = BFTW_SKIP_SUBTREE;
		goto done;
	}

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

done:
	if ((cmdline->debug & DEBUG_STAT) && ftwbuf->statbuf) {
		debug_stat(&state);
	}

	return state.action;
}

/**
 * Infer the number of open file descriptors we're allowed to have.
 */
static int infer_fdlimit(const struct cmdline *cmdline) {
	int ret = 4096;

	struct rlimit rl;
	if (getrlimit(RLIMIT_NOFILE, &rl) == 0) {
		if (rl.rlim_cur != RLIM_INFINITY) {
			ret = rl.rlim_cur;
		}
	}

	// 3 for std{in,out,err}
	int nopen = 3 + cmdline->nopen_files;

	// Check /dev/fd for the current number of open fds, if possible (we may
	// have inherited more than just the standard ones)
	DIR *dir = opendir("/dev/fd");
	if (dir) {
		// Account for 'dir' itself
		nopen = -1;

		while (true) {
			struct dirent *de;
			if (xreaddir(dir, &de) != 0 || !de) {
				break;
			}
			if (strcmp(de->d_name, ".") != 0 && strcmp(de->d_name, "..") != 0) {
				++nopen;
			}
		}

		closedir(dir);
	}

	// Extra fd needed by -empty
	int reserved = nopen + 1;

	if (ret > reserved) {
		ret -= reserved;
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

	if (cmdline->optlevel >= 4 && cmdline->expr->eval == eval_false) {
		if (cmdline->debug & DEBUG_OPT) {
			fputs("-O4: skipping evaluation of top-level -false\n", stderr);
		}
		return 0;
	}

	int nopenfd = infer_fdlimit(cmdline);

	struct callback_args args = {
		.cmdline = cmdline,
		.ret = 0,
		.quit = false,
	};

	for (struct root *root = cmdline->roots; root && !args.quit; root = root->next) {
		if (bftw(root->path, cmdline_callback, nopenfd, cmdline->flags, &args) != 0) {
			args.ret = -1;
			perror("bftw()");
		}
	}

	if (cmdline->debug & DEBUG_RATES) {
		dump_cmdline(cmdline, true);
	}

	return args.ret;
}
