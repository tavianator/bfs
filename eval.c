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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifndef S_ISDOOR
#	define S_ISDOOR(mode) false
#endif

#ifndef S_ISPORT
#	define S_ISPORT(mode) false
#endif

#ifndef S_ISWHT
#	define S_ISWHT(mode) false
#endif

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
		state->ret = -1;
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

static const char *exec_format_path(const struct expr *expr, const struct BFTW *ftwbuf) {
	if (!(expr->exec_flags & EXEC_CHDIR)) {
		return ftwbuf->path;
	}

	// For compatibility with GNU find, use './name' instead of just 'name'
	const char *name = ftwbuf->path + ftwbuf->nameoff;

	char *path = dstralloc(2 + strlen(name));
	if (!path) {
		perror("dstralloc()");
		return NULL;
	}

	if (dstrcat(&path, "./") != 0) {
		perror("dstrcat()");
		goto err;
	}
	if (dstrcat(&path, name) != 0) {
		perror("dstrcat()");
		goto err;
	}

	return path;

err:
	dstrfree(path);
	return NULL;
}

static void exec_free_path(const char *path, const struct BFTW *ftwbuf) {
	if (path != ftwbuf->path) {
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
		perror("dstralloc()");
		return NULL;
	}

	char *last = arg;
	do {
		if (dstrncat(&ret, last, match - last) != 0) {
			perror("dstrncat()");
			goto err;
		}
		if (dstrcat(&ret, path) != 0) {
			perror("dstrcat()");
			goto err;
		}

		last = match + 2;
		match = strstr(last, "{}");
	} while (match);

	if (dstrcat(&ret, last) != 0) {
		perror("dstrcat()");
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

	char *path = strdup(ftwbuf->path);
	if (!path) {
		perror("strdup()");
		_Exit(EXIT_FAILURE);
	}

	// Skip trailing slashes
	char *end = path + strlen(path);
	while (end > path && end[-1] == '/') {
		--end;
	}

	// Remove the last component
	while (end > path && end[-1] != '/') {
		--end;
	}
	if (end > path) {
		*end = '\0';
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
		goto out;
	}

	size_t argc = expr->argc - 2;
	char **template = expr->argv + 1;
	char **argv = exec_format_argv(argc, template, path);
	if (!argv) {
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
		perror("fork()");
		goto out_argv;
	} else if (pid > 0) {
		int status;
		if (waitpid(pid, &status, 0) < 0) {
			perror("waitpid()");
			goto out_argv;
		}

		ret = WIFEXITED(status) && WEXITSTATUS(status) == EXIT_SUCCESS;
	} else {
		if (expr->exec_flags & EXEC_CHDIR) {
			exec_chdir(ftwbuf);
		}

		if (expr->exec_flags & EXEC_CONFIRM) {
			if (redirect(STDIN_FILENO, "/dev/null", O_RDONLY) < 0) {
				perror("redirect()");
				goto exit;
			}
		}

		execvp(argv[0], argv);
		perror("execvp()");

	exit:
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
bool eval_print0(const struct expr *expr, struct eval_state *state) {
	const char *path = state->ftwbuf->path;
	size_t length = strlen(path) + 1;
	if (fwrite(path, 1, length, expr->file) != length) {
		eval_error(state);
	}
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
		[SIZE_MB] = 1024*1024,
		[SIZE_GB] = 1024*1024*1024,
	};

	off_t scale = scales[expr->size_unit];
	off_t size = (statbuf->st_size + scale - 1)/scale; // Round up
	return do_cmp(expr, size);
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

	switch ((enum bftw_typeflag)expr->idata) {
	case BFTW_UNKNOWN:
	case BFTW_ERROR:
		break;
	case BFTW_BLK:
		return S_ISBLK(sb.st_mode);
	case BFTW_CHR:
		return S_ISCHR(sb.st_mode);
	case BFTW_DIR:
		return S_ISDIR(sb.st_mode);
	case BFTW_DOOR:
		return S_ISDOOR(sb.st_mode);
	case BFTW_FIFO:
		return S_ISFIFO(sb.st_mode);
	case BFTW_LNK:
		return S_ISLNK(sb.st_mode);
	case BFTW_PORT:
		return S_ISPORT(sb.st_mode);
	case BFTW_REG:
		return S_ISREG(sb.st_mode);
	case BFTW_SOCK:
		return S_ISSOCK(sb.st_mode);
	case BFTW_WHT:
		return S_ISWHT(sb.st_mode);
	}

	return false;
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
		.ret = args->ret,
	};

	if (ftwbuf->typeflag == BFTW_ERROR) {
		if (!eval_should_ignore(&state, ftwbuf->error)) {
			state.ret = -1;
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

	args->ret = state.ret;
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

		struct dirent *de;
		while ((de = readdir(dir)) != NULL) {
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
	};

	for (struct root *root = cmdline->roots; root; root = root->next) {
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
