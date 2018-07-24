/****************************************************************************
 * bfs                                                                      *
 * Copyright (C) 2015-2017 Tavian Barnes <tavianator@tavianator.com>        *
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

#include "eval.h"
#include "bftw.h"
#include "cmdline.h"
#include "color.h"
#include "dstring.h"
#include "exec.h"
#include "mtab.h"
#include "printf.h"
#include "stat.h"
#include "util.h"
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <grp.h>
#include <pwd.h>
#include <stdint.h>
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
	int *ret;
	/** Whether to quit immediately. */
	bool *quit;
	/** A bfs_stat() buffer, if necessary. */
	struct bfs_stat statbuf;
	/** A bfs_stat() buffer for -xtype style tests. */
	struct bfs_stat xstatbuf;
};

/**
 * Check if an error should be ignored.
 */
static bool eval_should_ignore(const struct eval_state *state, int error) {
	return state->cmdline->ignore_races
		&& is_nonexistence_error(error)
		&& state->ftwbuf->depth > 0;
}

/**
 * Report an error that occurs during evaluation.
 */
static void eval_error(struct eval_state *state) {
	if (!eval_should_ignore(state, errno)) {
		cfprintf(state->cmdline->cerr, "%{er}error: '%s': %m%{rs}\n", state->ftwbuf->path);
		*state->ret = EXIT_FAILURE;
	}
}

#define DEBUG_FLAG(flags, flag)			\
	if ((flags & flag) || flags == flag) {	\
		fputs(#flag, stderr);		\
		flags ^= flag;			\
		if (flags) {			\
			fputs(" | ", stderr);	\
		}				\
	}

/**
 * Debug stat() calls.
 */
static void debug_stat(const struct eval_state *state, int at_flags, enum bfs_stat_flag flags) {
	if (!(state->cmdline->debug & DEBUG_STAT)) {
		return;
	}

	struct BFTW *ftwbuf = state->ftwbuf;

	fprintf(stderr, "bfs_stat(");
	if (ftwbuf->at_fd == AT_FDCWD) {
		fprintf(stderr, "AT_FDCWD");
	} else {
		size_t baselen = strlen(ftwbuf->path) - strlen(ftwbuf->at_path);
		fprintf(stderr, "\"");
		fwrite(ftwbuf->path, 1, baselen, stderr);
		fprintf(stderr, "\"");
	}

	fprintf(stderr, ", \"%s\", ", ftwbuf->at_path);

	DEBUG_FLAG(at_flags, 0);
	DEBUG_FLAG(at_flags, AT_SYMLINK_NOFOLLOW);

	fprintf(stderr, ", ");

	DEBUG_FLAG(flags, 0);
	DEBUG_FLAG(flags, BFS_STAT_BROKEN_OK);

	fprintf(stderr, ")\n");
}

/**
 * Perform a bfs_stat() call if necessary.
 */
static const struct bfs_stat *eval_stat(struct eval_state *state) {
	struct BFTW *ftwbuf = state->ftwbuf;
	if (!ftwbuf->statbuf) {
		debug_stat(state, ftwbuf->at_flags, BFS_STAT_BROKEN_OK);
		if (bfs_stat(ftwbuf->at_fd, ftwbuf->at_path, ftwbuf->at_flags, BFS_STAT_BROKEN_OK, &state->statbuf) == 0) {
			ftwbuf->statbuf = &state->statbuf;
		} else {
			eval_error(state);
		}
	}
	return ftwbuf->statbuf;
}

/**
 * Perform a bfs_stat() call for tests that flip the follow flag, like -xtype.
 */
static const struct bfs_stat *eval_xstat(struct eval_state *state) {
	struct BFTW *ftwbuf = state->ftwbuf;
	struct bfs_stat *statbuf = &state->xstatbuf;
	if (!statbuf->mask) {
		int at_flags = ftwbuf->at_flags ^ AT_SYMLINK_NOFOLLOW;
		debug_stat(state, at_flags, 0);
		if (bfs_stat(ftwbuf->at_fd, ftwbuf->at_path, at_flags, 0, statbuf) != 0) {
			return NULL;
		}
	}
	return statbuf;
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

bool expr_cmp(const struct expr *expr, long long n) {
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
	return xfaccessat(ftwbuf->at_fd, ftwbuf->at_path, expr->idata) == 0;
}

/**
 * Get the given timespec field out of a stat buffer.
 */
static const struct timespec *eval_stat_time(const struct expr *expr, struct eval_state *state) {
	const struct bfs_stat *statbuf = eval_stat(state);
	if (!statbuf) {
		return NULL;
	}

	if (!(statbuf->mask & expr->stat_field)) {
		assert(expr->stat_field == BFS_STAT_BTIME);
		cfprintf(state->cmdline->cerr, "%{er}error: '%s': Couldn't get file birth time.%{rs}\n", state->ftwbuf->path);
		*state->ret = EXIT_FAILURE;
		return NULL;
	}

	switch (expr->stat_field) {
	case BFS_STAT_ATIME:
		return &statbuf->atime;
	case BFS_STAT_BTIME:
		return &statbuf->btime;
	case BFS_STAT_CTIME:
		return &statbuf->ctime;
	case BFS_STAT_MTIME:
		return &statbuf->mtime;
	default:
		assert(false);
		return NULL;
	}
}

/**
 * -[aBcm]?newer tests.
 */
bool eval_newer(const struct expr *expr, struct eval_state *state) {
	const struct timespec *time = eval_stat_time(expr, state);
	if (!time) {
		return false;
	}

	return time->tv_sec > expr->reftime.tv_sec
		|| (time->tv_sec == expr->reftime.tv_sec && time->tv_nsec > expr->reftime.tv_nsec);
}

/**
 * -[aBcm]{min,time} tests.
 */
bool eval_time(const struct expr *expr, struct eval_state *state) {
	const struct timespec *time = eval_stat_time(expr, state);
	if (!time) {
		return false;
	}

	time_t diff = timespec_diff(&expr->reftime, time);
	switch (expr->time_unit) {
	case MINUTES:
		diff /= 60;
		break;
	case DAYS:
		diff /= 60*60*24;
		break;
	}

	return expr_cmp(expr, diff);
}

/**
 * -used test.
 */
bool eval_used(const struct expr *expr, struct eval_state *state) {
	const struct bfs_stat *statbuf = eval_stat(state);
	if (!statbuf) {
		return false;
	}

	time_t diff = timespec_diff(&statbuf->atime, &statbuf->ctime);
	diff /= 60*60*24;
	return expr_cmp(expr, diff);
}

/**
 * -gid test.
 */
bool eval_gid(const struct expr *expr, struct eval_state *state) {
	const struct bfs_stat *statbuf = eval_stat(state);
	if (!statbuf) {
		return false;
	}

	return expr_cmp(expr, statbuf->gid);
}

/**
 * -uid test.
 */
bool eval_uid(const struct expr *expr, struct eval_state *state) {
	const struct bfs_stat *statbuf = eval_stat(state);
	if (!statbuf) {
		return false;
	}

	return expr_cmp(expr, statbuf->uid);
}

/**
 * -nogroup test.
 */
bool eval_nogroup(const struct expr *expr, struct eval_state *state) {
	const struct bfs_stat *statbuf = eval_stat(state);
	if (!statbuf) {
		return false;
	}

	return getgrgid(statbuf->gid) == NULL;
}

/**
 * -nouser test.
 */
bool eval_nouser(const struct expr *expr, struct eval_state *state) {
	const struct bfs_stat *statbuf = eval_stat(state);
	if (!statbuf) {
		return false;
	}

	return getpwuid(statbuf->uid) == NULL;
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
	if (ftwbuf->at_flags & AT_SYMLINK_NOFOLLOW) {
		if (ftwbuf->typeflag == BFTW_DIR) {
			flag |= AT_REMOVEDIR;
		}
	} else if (ftwbuf->typeflag != BFTW_LNK) {
		// We need to know the actual type of the path, not what it points to
		const struct bfs_stat *statbuf = eval_xstat(state);
		if (statbuf) {
			if (S_ISDIR(statbuf->mode)) {
				flag |= AT_REMOVEDIR;
			}
		} else {
			eval_error(state);
			return false;
		}
	}

	if (unlinkat(ftwbuf->at_fd, ftwbuf->at_path, flag) != 0) {
		eval_error(state);
		return false;
	}

	return true;
}

/** Finish any pending -exec ... + operations. */
static int eval_exec_finish(const struct expr *expr, const struct cmdline *cmdline) {
	int ret = 0;
	if (expr->execbuf && bfs_exec_finish(expr->execbuf) != 0) {
		if (errno != 0) {
			cfprintf(cmdline->cerr, "%{er}error: %s %s: %m%{rs}\n", expr->argv[0], expr->argv[1]);
		}
		ret = -1;
	}
	if (expr->lhs && eval_exec_finish(expr->lhs, cmdline) != 0) {
		ret = -1;
	}
	if (expr->rhs && eval_exec_finish(expr->rhs, cmdline) != 0) {
		ret = -1;
	}
	return ret;
}

/**
 * -exec[dir]/-ok[dir] actions.
 */
bool eval_exec(const struct expr *expr, struct eval_state *state) {
	bool ret = bfs_exec(expr->execbuf, state->ftwbuf) == 0;
	if (errno != 0) {
		cfprintf(state->cmdline->cerr, "%{er}error: %s %s: %m%{rs}\n", expr->argv[0], expr->argv[1]);
		*state->ret = EXIT_FAILURE;
	}
	return ret;
}

/**
 * -exit action.
 */
bool eval_exit(const struct expr *expr, struct eval_state *state) {
	state->action = BFTW_STOP;
	*state->ret = expr->idata;
	*state->quit = true;
	return true;
}

/**
 * -depth N test.
 */
bool eval_depth(const struct expr *expr, struct eval_state *state) {
	return expr_cmp(expr, state->ftwbuf->depth);
}

/**
 * -empty test.
 */
bool eval_empty(const struct expr *expr, struct eval_state *state) {
	bool ret = false;
	struct BFTW *ftwbuf = state->ftwbuf;

	if (ftwbuf->typeflag == BFTW_DIR) {
		int dfd = openat(ftwbuf->at_fd, ftwbuf->at_path, O_RDONLY | O_CLOEXEC | O_DIRECTORY);
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
		const struct bfs_stat *statbuf = eval_stat(state);
		if (statbuf) {
			ret = statbuf->size == 0;
		}
	}

done:
	return ret;
}

/**
 * -fstype test.
 */
bool eval_fstype(const struct expr *expr, struct eval_state *state) {
	const struct bfs_stat *statbuf = eval_stat(state);
	if (!statbuf) {
		return false;
	}

	const char *type = bfs_fstype(state->cmdline->mtab, statbuf);
	return strcmp(type, expr->sdata) == 0;
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
	const struct bfs_stat *statbuf = eval_stat(state);
	if (!statbuf) {
		return false;
	}

	return expr_cmp(expr, statbuf->ino);
}

/**
 * -links test.
 */
bool eval_links(const struct expr *expr, struct eval_state *state) {
	const struct bfs_stat *statbuf = eval_stat(state);
	if (!statbuf) {
		return false;
	}

	return expr_cmp(expr, statbuf->nlink);
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

	const struct bfs_stat *statbuf = eval_stat(state);
	if (!statbuf) {
		goto done;
	}

	name = xreadlinkat(ftwbuf->at_fd, ftwbuf->at_path, statbuf->size);
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
	const struct bfs_stat *statbuf = eval_stat(state);
	if (!statbuf) {
		return false;
	}

	mode_t mode = statbuf->mode;
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
 * -f?ls action.
 */
bool eval_fls(const struct expr *expr, struct eval_state *state) {
	CFILE *cfile = expr->cfile;
	FILE *file = cfile->file;
	const struct BFTW *ftwbuf = state->ftwbuf;
	const struct bfs_stat *statbuf = eval_stat(state);
	if (!statbuf) {
		goto done;
	}

	uintmax_t ino = statbuf->ino;
	uintmax_t blocks = ((uintmax_t)statbuf->blocks*BFS_STAT_BLKSIZE + 1023)/1024;
	char mode[11];
	format_mode(statbuf->mode, mode);
	uintmax_t nlink = statbuf->nlink;
	if (fprintf(file, "%9ju %6ju %s %3ju ", ino, blocks, mode, nlink) < 0) {
		goto error;
	}

	uintmax_t uid = statbuf->uid;
	struct passwd *pwd = getpwuid(uid);
	if (pwd) {
		if (fprintf(file, " %-8s", pwd->pw_name) < 0) {
			goto error;
		}
	} else {
		if (fprintf(file, " %-8ju", uid) < 0) {
			goto error;
		}
	}

	uintmax_t gid = statbuf->gid;
	struct group *grp = getgrgid(gid);
	if (grp) {
		if (fprintf(file, " %-8s", grp->gr_name) < 0) {
			goto error;
		}
	} else {
		if (fprintf(file, " %-8ju", gid) < 0) {
			goto error;
		}
	}

	uintmax_t size = statbuf->size;
	if (fprintf(file, " %8ju", size) < 0) {
		goto error;
	}

	time_t time = statbuf->mtime.tv_sec;
	time_t now = expr->reftime.tv_sec;
	time_t six_months_ago = now - 6*30*24*60*60;
	time_t tomorrow = now + 24*60*60;
	struct tm tm;
	if (xlocaltime(&time, &tm) != 0) {
		goto error;
	}
	char time_str[256];
	const char *time_format = "%b %e %H:%M";
	if (time <= six_months_ago || time >= tomorrow) {
		time_format = "%b %e  %Y";
	}
	if (!strftime(time_str, sizeof(time_str), time_format, &tm)) {
		errno = EOVERFLOW;
		goto error;
	}
	if (fprintf(file, " %s", time_str) < 0) {
		goto error;
	}

	if (cfprintf(cfile, " %P", ftwbuf) < 0) {
		goto error;
	}

	if (ftwbuf->typeflag == BFTW_LNK) {
		if (cfprintf(cfile, " -> %L", ftwbuf) < 0) {
			goto error;
		}
	}

	if (fputc('\n', file) == EOF) {
		goto error;
	}

done:
	return true;

error:
	eval_error(state);
	return true;
}

/**
 * -f?print action.
 */
bool eval_fprint(const struct expr *expr, struct eval_state *state) {
	CFILE *cfile = expr->cfile;
	if (cfile->colors) {
		eval_stat(state);
	}

	if (cfprintf(cfile, "%P\n", state->ftwbuf) < 0) {
		eval_error(state);
	}

	return true;
}

/**
 * -f?print0 action.
 */
bool eval_fprint0(const struct expr *expr, struct eval_state *state) {
	const char *path = state->ftwbuf->path;
	size_t length = strlen(path) + 1;
	if (fwrite(path, 1, length, expr->cfile->file) != length) {
		eval_error(state);
	}
	return true;
}

/**
 * -f?printf action.
 */
bool eval_fprintf(const struct expr *expr, struct eval_state *state) {
	if (expr->printf->needs_stat) {
		if (!eval_stat(state)) {
			goto done;
		}
	}

	if (bfs_printf(expr->cfile->file, expr->printf, state->ftwbuf) != 0) {
		eval_error(state);
	}

done:
	return true;
}

/**
 * -printx action.
 */
bool eval_fprintx(const struct expr *expr, struct eval_state *state) {
	FILE *file = expr->cfile->file;
	const char *path = state->ftwbuf->path;

	while (true) {
		size_t span = strcspn(path, " \t\n\\$'\"`");
		if (fwrite(path, 1, span, file) != span) {
			goto error;
		}
		path += span;

		char c = path[0];
		if (!c) {
			break;
		}

		char escaped[] = {'\\', c};
		if (fwrite(escaped, 1, sizeof(escaped), file) != sizeof(escaped)) {
			goto error;
		}
		++path;
	}


	if (fputc('\n', file) == EOF) {
		goto error;
	}

	return true;

error:
	eval_error(state);
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
			cfprintf(state->cmdline->cerr, "%{er}error: '%s': %s%{rs}\n", path, str);
			free(str);
		} else {
			perror("xregerror()");
		}

		*state->ret = EXIT_FAILURE;
	}

	return false;
}

/**
 * -samefile test.
 */
bool eval_samefile(const struct expr *expr, struct eval_state *state) {
	const struct bfs_stat *statbuf = eval_stat(state);
	if (!statbuf) {
		return false;
	}

	return statbuf->dev == expr->dev && statbuf->ino == expr->ino;
}

/**
 * -size test.
 */
bool eval_size(const struct expr *expr, struct eval_state *state) {
	const struct bfs_stat *statbuf = eval_stat(state);
	if (!statbuf) {
		return false;
	}

	static const off_t scales[] = {
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
	off_t size = (statbuf->size + scale - 1)/scale; // Round up
	return expr_cmp(expr, size);
}

/**
 * -sparse test.
 */
bool eval_sparse(const struct expr *expr, struct eval_state *state) {
	const struct bfs_stat *statbuf = eval_stat(state);
	if (!statbuf) {
		return false;
	}

	blkcnt_t expected = (statbuf->size + BFS_STAT_BLKSIZE - 1)/BFS_STAT_BLKSIZE;
	return statbuf->blocks < expected;
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

	bool follow = !(ftwbuf->at_flags & AT_SYMLINK_NOFOLLOW);
	bool is_link = ftwbuf->typeflag == BFTW_LNK;
	if (follow == is_link) {
		return eval_type(expr, state);
	}

	const struct bfs_stat *statbuf = eval_xstat(state);
	if (statbuf) {
		return mode_to_typeflag(statbuf->mode) & expr->idata;
	} else if (!follow && is_nonexistence_error(errno)) {
		// Broken symlink
		return eval_type(expr, state);
	} else {
		eval_error(state);
		return false;
	}
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

	assert(!*state->quit);

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

	if (expr_never_returns(expr)) {
		assert(*state->quit);
	} else if (!*state->quit) {
		assert(!expr->always_true || ret);
		assert(!expr->always_false || !ret);
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
 * Dump the bftw_typeflag for -D search.
 */
static const char *dump_bftw_typeflag(enum bftw_typeflag type) {
#define DUMP_BFTW_TYPEFLAG_CASE(flag)		\
	case flag:				\
		return #flag

	switch (type) {
		DUMP_BFTW_TYPEFLAG_CASE(BFTW_BLK);
		DUMP_BFTW_TYPEFLAG_CASE(BFTW_CHR);
		DUMP_BFTW_TYPEFLAG_CASE(BFTW_DIR);
		DUMP_BFTW_TYPEFLAG_CASE(BFTW_DOOR);
		DUMP_BFTW_TYPEFLAG_CASE(BFTW_FIFO);
		DUMP_BFTW_TYPEFLAG_CASE(BFTW_LNK);
		DUMP_BFTW_TYPEFLAG_CASE(BFTW_PORT);
		DUMP_BFTW_TYPEFLAG_CASE(BFTW_REG);
		DUMP_BFTW_TYPEFLAG_CASE(BFTW_SOCK);
		DUMP_BFTW_TYPEFLAG_CASE(BFTW_WHT);

		DUMP_BFTW_TYPEFLAG_CASE(BFTW_ERROR);

	default:
		DUMP_BFTW_TYPEFLAG_CASE(BFTW_UNKNOWN);
	}
}

#define DUMP_BFTW_MAP(value) [value] = #value

/**
 * Dump the bftw_visit for -D search.
 */
static const char *dump_bftw_visit(enum bftw_visit visit) {
	static const char *visits[] = {
		DUMP_BFTW_MAP(BFTW_PRE),
		DUMP_BFTW_MAP(BFTW_POST),
	};
	return visits[visit];
}

/**
 * Dump the bftw_action for -D search.
 */
static const char *dump_bftw_action(enum bftw_action action) {
	static const char *actions[] = {
		DUMP_BFTW_MAP(BFTW_CONTINUE),
		DUMP_BFTW_MAP(BFTW_SKIP_SIBLINGS),
		DUMP_BFTW_MAP(BFTW_SKIP_SUBTREE),
		DUMP_BFTW_MAP(BFTW_STOP),
	};
	return actions[action];
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

	struct eval_state state;
	state.ftwbuf = ftwbuf;
	state.cmdline = cmdline;
	state.action = BFTW_CONTINUE;
	state.ret = &args->ret;
	state.quit = &args->quit;
	state.xstatbuf.mask = 0;

	if (ftwbuf->statbuf) {
		debug_stat(&state, ftwbuf->at_flags, BFS_STAT_BROKEN_OK);
	}

	if (ftwbuf->typeflag == BFTW_ERROR) {
		if (!eval_should_ignore(&state, ftwbuf->error)) {
			args->ret = EXIT_FAILURE;
			cfprintf(cmdline->cerr, "%{er}error: '%s': %s%{rs}\n", ftwbuf->path, strerror(ftwbuf->error));
		}
		state.action = BFTW_SKIP_SUBTREE;
		goto done;
	}

	if (cmdline->xargs_safe && strpbrk(ftwbuf->path, " \t\n\'\"\\")) {
		args->ret = EXIT_FAILURE;
		cfprintf(cmdline->cerr, "%{er}error: '%s': Path is not safe for xargs.%{rs}\n", ftwbuf->path);
		state.action = BFTW_SKIP_SUBTREE;
		goto done;
	}

	if (cmdline->maxdepth < 0 || ftwbuf->depth >= cmdline->maxdepth) {
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
	if (cmdline->debug & DEBUG_SEARCH) {
		fprintf(stderr,
		        "cmdline_callback({ "
		        ".path = \"%s\", "
		        ".depth = %zu, "
		        ".visit = %s, "
		        ".typeflag = %s, "
		        ".error = %d "
		        "}) == %s\n",
		        ftwbuf->path,
		        ftwbuf->depth,
		        dump_bftw_visit(ftwbuf->visit),
			dump_bftw_typeflag(ftwbuf->typeflag),
			ftwbuf->error,
		        dump_bftw_action(state.action));
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

	// Check /proc/self/fd for the current number of open fds, if possible
	// (we may have inherited more than just the standard ones)
	DIR *dir = opendir("/proc/self/fd");
	if (!dir) {
		dir = opendir("/dev/fd");
	}
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

	ret -= nopen;
	ret -= cmdline->expr->persistent_fds;
	ret -= cmdline->expr->ephemeral_fds;

	// bftw() needs at least 2 available fds
	if (ret < 2) {
		ret = 2;
	}

	return ret;
}

/**
 * Dump the bftw() flags for -D search.
 */
static void dump_bftw_flags(enum bftw_flags flags) {
	DEBUG_FLAG(flags, 0);
	DEBUG_FLAG(flags, BFTW_STAT);
	DEBUG_FLAG(flags, BFTW_RECOVER);
	DEBUG_FLAG(flags, BFTW_DEPTH);
	DEBUG_FLAG(flags, BFTW_COMFOLLOW);
	DEBUG_FLAG(flags, BFTW_LOGICAL);
	DEBUG_FLAG(flags, BFTW_DETECT_CYCLES);
	DEBUG_FLAG(flags, BFTW_XDEV);

	assert(!flags);
}

/**
 * Evaluate the command line.
 */
int eval_cmdline(const struct cmdline *cmdline) {
	if (!cmdline->expr) {
		return EXIT_SUCCESS;
	}

	int nopenfd = infer_fdlimit(cmdline);

	struct callback_args args = {
		.cmdline = cmdline,
		.ret = EXIT_SUCCESS,
		.quit = false,
	};

	for (struct root *root = cmdline->roots; root && !args.quit; root = root->next) {
		if (cmdline->debug & DEBUG_SEARCH) {
			fprintf(stderr, "bftw(\"%s\", cmdline_callback, %d, ", root->path, nopenfd);
			dump_bftw_flags(cmdline->flags);
			fprintf(stderr, ", &args)\n");
		}

		if (bftw(root->path, cmdline_callback, nopenfd, cmdline->flags, &args) != 0) {
			args.ret = EXIT_FAILURE;
			perror("bftw()");
		}
	}

	if (eval_exec_finish(cmdline->expr, cmdline) != 0) {
		args.ret = EXIT_FAILURE;
	}

	if (cmdline->debug & DEBUG_RATES) {
		dump_cmdline(cmdline, true);
	}

	return args.ret;
}
