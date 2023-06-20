// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

/**
 * Implementation of all the primary expressions.
 */

#include "eval.h"
#include "bar.h"
#include "bfstd.h"
#include "bftw.h"
#include "color.h"
#include "config.h"
#include "ctx.h"
#include "darray.h"
#include "diag.h"
#include "dir.h"
#include "dstring.h"
#include "exec.h"
#include "expr.h"
#include "fsade.h"
#include "mtab.h"
#include "printf.h"
#include "pwcache.h"
#include "stat.h"
#include "trie.h"
#include "xregex.h"
#include "xtime.h"
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <grp.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <wchar.h>

struct bfs_eval {
	/** Data about the current file. */
	const struct BFTW *ftwbuf;
	/** The bfs context. */
	const struct bfs_ctx *ctx;
	/** The bftw() callback return value. */
	enum bftw_action action;
	/** The bfs_eval() return value. */
	int *ret;
	/** Whether to quit immediately. */
	bool quit;
};

/**
 * Print an error message.
 */
BFS_FORMATTER(2, 3)
static void eval_error(struct bfs_eval *state, const char *format, ...) {
	// By POSIX, any errors should be accompanied by a non-zero exit status
	*state->ret = EXIT_FAILURE;

	int error = errno;
	const struct bfs_ctx *ctx = state->ctx;
	CFILE *cerr = ctx->cerr;

	bfs_error(ctx, "%pP: ", state->ftwbuf);

	va_list args;
	va_start(args, format);
	errno = error;
	cvfprintf(cerr, format, args);
	va_end(args);
}

/**
 * Check if an error should be ignored.
 */
static bool eval_should_ignore(const struct bfs_eval *state, int error) {
	return state->ctx->ignore_races
		&& is_nonexistence_error(error)
		&& state->ftwbuf->depth > 0;
}

/**
 * Report an error that occurs during evaluation.
 */
static void eval_report_error(struct bfs_eval *state) {
	if (!eval_should_ignore(state, errno)) {
		eval_error(state, "%m.\n");
	}
}

/**
 * Report an I/O error that occurs during evaluation.
 */
static void eval_io_error(const struct bfs_expr *expr, struct bfs_eval *state) {
	if (expr->path) {
		eval_error(state, "'%s': %m.\n", expr->path);
	} else {
		eval_error(state, "(standard output): %m.\n");
	}

	// Don't report the error again in bfs_ctx_free()
	clearerr(expr->cfile->file);
}

/**
 * Perform a bfs_stat() call if necessary.
 */
static const struct bfs_stat *eval_stat(struct bfs_eval *state) {
	const struct BFTW *ftwbuf = state->ftwbuf;
	const struct bfs_stat *ret = bftw_stat(ftwbuf, ftwbuf->stat_flags);
	if (!ret) {
		eval_report_error(state);
	}
	return ret;
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

bool bfs_expr_cmp(const struct bfs_expr *expr, long long n) {
	switch (expr->int_cmp) {
	case BFS_INT_EQUAL:
		return n == expr->num;
	case BFS_INT_LESS:
		return n < expr->num;
	case BFS_INT_GREATER:
		return n > expr->num;
	}

	bfs_bug("Invalid comparison mode");
	return false;
}

/**
 * -true test.
 */
bool eval_true(const struct bfs_expr *expr, struct bfs_eval *state) {
	return true;
}

/**
 * -false test.
 */
bool eval_false(const struct bfs_expr *expr, struct bfs_eval *state) {
	return false;
}

/**
 * -executable, -readable, -writable tests.
 */
bool eval_access(const struct bfs_expr *expr, struct bfs_eval *state) {
	const struct BFTW *ftwbuf = state->ftwbuf;
	return xfaccessat(ftwbuf->at_fd, ftwbuf->at_path, expr->num) == 0;
}

/**
 * -acl test.
 */
bool eval_acl(const struct bfs_expr *expr, struct bfs_eval *state) {
	int ret = bfs_check_acl(state->ftwbuf);
	if (ret >= 0) {
		return ret;
	} else {
		eval_report_error(state);
		return false;
	}
}

/**
 * -capable test.
 */
bool eval_capable(const struct bfs_expr *expr, struct bfs_eval *state) {
	int ret = bfs_check_capabilities(state->ftwbuf);
	if (ret >= 0) {
		return ret;
	} else {
		eval_report_error(state);
		return false;
	}
}

/**
 * Get the given timespec field out of a stat buffer.
 */
static const struct timespec *eval_stat_time(const struct bfs_stat *statbuf, enum bfs_stat_field field, struct bfs_eval *state) {
	const struct timespec *ret = bfs_stat_time(statbuf, field);
	if (!ret) {
		eval_error(state, "Couldn't get file %s: %m.\n", bfs_stat_field_name(field));
	}
	return ret;
}

/**
 * -[aBcm]?newer tests.
 */
bool eval_newer(const struct bfs_expr *expr, struct bfs_eval *state) {
	const struct bfs_stat *statbuf = eval_stat(state);
	if (!statbuf) {
		return false;
	}

	const struct timespec *time = eval_stat_time(statbuf, expr->stat_field, state);
	if (!time) {
		return false;
	}

	return time->tv_sec > expr->reftime.tv_sec
		|| (time->tv_sec == expr->reftime.tv_sec && time->tv_nsec > expr->reftime.tv_nsec);
}

/**
 * -[aBcm]{min,time} tests.
 */
bool eval_time(const struct bfs_expr *expr, struct bfs_eval *state) {
	const struct bfs_stat *statbuf = eval_stat(state);
	if (!statbuf) {
		return false;
	}

	const struct timespec *time = eval_stat_time(statbuf, expr->stat_field, state);
	if (!time) {
		return false;
	}

	time_t diff = timespec_diff(&expr->reftime, time);
	switch (expr->time_unit) {
	case BFS_DAYS:
		diff /= 60*24;
		fallthru;
	case BFS_MINUTES:
		diff /= 60;
		fallthru;
	case BFS_SECONDS:
		break;
	}

	return bfs_expr_cmp(expr, diff);
}

/**
 * -used test.
 */
bool eval_used(const struct bfs_expr *expr, struct bfs_eval *state) {
	const struct bfs_stat *statbuf = eval_stat(state);
	if (!statbuf) {
		return false;
	}

	const struct timespec *atime = eval_stat_time(statbuf, BFS_STAT_ATIME, state);
	const struct timespec *ctime = eval_stat_time(statbuf, BFS_STAT_CTIME, state);
	if (!atime || !ctime) {
		return false;
	}

	long long diff = timespec_diff(atime, ctime);
	if (diff < 0) {
		return false;
	}

	long long day_seconds = 60*60*24;
	diff = (diff + day_seconds - 1) / day_seconds;
	return bfs_expr_cmp(expr, diff);
}

/**
 * -gid test.
 */
bool eval_gid(const struct bfs_expr *expr, struct bfs_eval *state) {
	const struct bfs_stat *statbuf = eval_stat(state);
	if (!statbuf) {
		return false;
	}

	return bfs_expr_cmp(expr, statbuf->gid);
}

/**
 * -uid test.
 */
bool eval_uid(const struct bfs_expr *expr, struct bfs_eval *state) {
	const struct bfs_stat *statbuf = eval_stat(state);
	if (!statbuf) {
		return false;
	}

	return bfs_expr_cmp(expr, statbuf->uid);
}

/**
 * -nogroup test.
 */
bool eval_nogroup(const struct bfs_expr *expr, struct bfs_eval *state) {
	const struct bfs_stat *statbuf = eval_stat(state);
	if (!statbuf) {
		return false;
	}

	const struct group *grp = bfs_getgrgid(state->ctx->groups, statbuf->gid);
	if (errno != 0) {
		eval_report_error(state);
	}
	return grp == NULL;
}

/**
 * -nouser test.
 */
bool eval_nouser(const struct bfs_expr *expr, struct bfs_eval *state) {
	const struct bfs_stat *statbuf = eval_stat(state);
	if (!statbuf) {
		return false;
	}

	const struct passwd *pwd = bfs_getpwuid(state->ctx->users, statbuf->uid);
	if (errno != 0) {
		eval_report_error(state);
	}
	return pwd == NULL;
}

/**
 * -delete action.
 */
bool eval_delete(const struct bfs_expr *expr, struct bfs_eval *state) {
	const struct BFTW *ftwbuf = state->ftwbuf;

	// Don't try to delete the current directory
	if (strcmp(ftwbuf->path, ".") == 0) {
		return true;
	}

	int flag = 0;

	// We need to know the actual type of the path, not what it points to
	enum bfs_type type = bftw_type(ftwbuf, BFS_STAT_NOFOLLOW);
	if (type == BFS_DIR) {
		flag |= AT_REMOVEDIR;
	} else if (type == BFS_ERROR) {
		eval_report_error(state);
		return false;
	}

	if (unlinkat(ftwbuf->at_fd, ftwbuf->at_path, flag) != 0) {
		eval_report_error(state);
		return false;
	}

	return true;
}

/** Finish any pending -exec ... + operations. */
static int eval_exec_finish(const struct bfs_expr *expr, const struct bfs_ctx *ctx) {
	int ret = 0;

	if (expr->eval_fn == eval_exec) {
		if (bfs_exec_finish(expr->exec) != 0) {
			if (errno != 0) {
				bfs_error(ctx, "%s %s: %m.\n", expr->argv[0], expr->argv[1]);
			}
			ret = -1;
		}
	} else if (bfs_expr_is_parent(expr)) {
		if (expr->lhs && eval_exec_finish(expr->lhs, ctx) != 0) {
			ret = -1;
		}
		if (expr->rhs && eval_exec_finish(expr->rhs, ctx) != 0) {
			ret = -1;
		}
	}

	return ret;
}

/**
 * -exec[dir]/-ok[dir] actions.
 */
bool eval_exec(const struct bfs_expr *expr, struct bfs_eval *state) {
	bool ret = bfs_exec(expr->exec, state->ftwbuf) == 0;
	if (errno != 0) {
		eval_error(state, "%s %s: %m.\n", expr->argv[0], expr->argv[1]);
	}
	return ret;
}

/**
 * -exit action.
 */
bool eval_exit(const struct bfs_expr *expr, struct bfs_eval *state) {
	state->action = BFTW_STOP;
	*state->ret = expr->num;
	state->quit = true;
	return true;
}

/**
 * -depth N test.
 */
bool eval_depth(const struct bfs_expr *expr, struct bfs_eval *state) {
	return bfs_expr_cmp(expr, state->ftwbuf->depth);
}

/**
 * -empty test.
 */
bool eval_empty(const struct bfs_expr *expr, struct bfs_eval *state) {
	bool ret = false;
	const struct BFTW *ftwbuf = state->ftwbuf;

	if (ftwbuf->type == BFS_DIR) {
		struct bfs_dir *dir = bfs_allocdir();
		if (!dir) {
			eval_report_error(state);
			return ret;
		}

		if (bfs_opendir(dir, ftwbuf->at_fd, ftwbuf->at_path) != 0) {
			eval_report_error(state);
			return ret;
		}

		int did_read = bfs_readdir(dir, NULL);
		if (did_read < 0) {
			eval_report_error(state);
		} else {
			ret = !did_read;
		}

		bfs_closedir(dir);
		free(dir);
	} else if (ftwbuf->type == BFS_REG) {
		const struct bfs_stat *statbuf = eval_stat(state);
		if (statbuf) {
			ret = statbuf->size == 0;
		}
	}

	return ret;
}

/**
 * -flags test.
 */
bool eval_flags(const struct bfs_expr *expr, struct bfs_eval *state) {
	const struct bfs_stat *statbuf = eval_stat(state);
	if (!statbuf) {
		return false;
	}

	if (!(statbuf->mask & BFS_STAT_ATTRS)) {
		eval_error(state, "Couldn't get file %s.\n", bfs_stat_field_name(BFS_STAT_ATTRS));
		return false;
	}

	unsigned long flags = statbuf->attrs;
	unsigned long set = expr->set_flags;
	unsigned long clear = expr->clear_flags;

	switch (expr->flags_cmp) {
	case BFS_MODE_EQUAL:
		return flags == set && !(flags & clear);

	case BFS_MODE_ALL:
		return (flags & set) == set && !(flags & clear);

	case BFS_MODE_ANY:
		return (flags & set) || (flags & clear) != clear;
	}

	bfs_bug("Invalid comparison mode");
	return false;
}

/**
 * -fstype test.
 */
bool eval_fstype(const struct bfs_expr *expr, struct bfs_eval *state) {
	const struct bfs_stat *statbuf = eval_stat(state);
	if (!statbuf) {
		return false;
	}

	const struct bfs_mtab *mtab = bfs_ctx_mtab(state->ctx);
	if (!mtab) {
		eval_report_error(state);
		return false;
	}

	const char *type = bfs_fstype(mtab, statbuf);
	if (!type) {
		eval_report_error(state);
		return false;
	}

	return strcmp(type, expr->argv[1]) == 0;
}

/**
 * -hidden test.
 */
bool eval_hidden(const struct bfs_expr *expr, struct bfs_eval *state) {
	const struct BFTW *ftwbuf = state->ftwbuf;
	const char *name = ftwbuf->path + ftwbuf->nameoff;

	// Don't treat "." or ".." as hidden directories.  Otherwise we'd filter
	// out everything when given
	//
	//     $ bfs . -nohidden
	//     $ bfs .. -nohidden
	return name[0] == '.' && strcmp(name, ".") != 0 && strcmp(name, "..") != 0;
}

/**
 * -inum test.
 */
bool eval_inum(const struct bfs_expr *expr, struct bfs_eval *state) {
	const struct bfs_stat *statbuf = eval_stat(state);
	if (!statbuf) {
		return false;
	}

	return bfs_expr_cmp(expr, statbuf->ino);
}

/**
 * -links test.
 */
bool eval_links(const struct bfs_expr *expr, struct bfs_eval *state) {
	const struct bfs_stat *statbuf = eval_stat(state);
	if (!statbuf) {
		return false;
	}

	return bfs_expr_cmp(expr, statbuf->nlink);
}

/**
 * -i?lname test.
 */
bool eval_lname(const struct bfs_expr *expr, struct bfs_eval *state) {
	bool ret = false;
	char *name = NULL;

	const struct BFTW *ftwbuf = state->ftwbuf;
	if (ftwbuf->type != BFS_LNK) {
		goto done;
	}

	const struct bfs_stat *statbuf = bftw_cached_stat(ftwbuf, BFS_STAT_NOFOLLOW);
	size_t len = statbuf ? statbuf->size : 0;

	name = xreadlinkat(ftwbuf->at_fd, ftwbuf->at_path, len);
	if (!name) {
		eval_report_error(state);
		goto done;
	}

	ret = fnmatch(expr->argv[1], name, expr->num) == 0;

done:
	free(name);
	return ret;
}

/**
 * -i?name test.
 */
bool eval_name(const struct bfs_expr *expr, struct bfs_eval *state) {
	const struct BFTW *ftwbuf = state->ftwbuf;

	const char *name = ftwbuf->path + ftwbuf->nameoff;
	char *copy = NULL;
	if (ftwbuf->depth == 0) {
		// Any trailing slashes are not part of the name.  This can only
		// happen for the root path.
		name = copy = xbasename(name);
	}

	bool ret = fnmatch(expr->argv[1], name, expr->num) == 0;
	free(copy);
	return ret;
}

/**
 * -i?path test.
 */
bool eval_path(const struct bfs_expr *expr, struct bfs_eval *state) {
	const struct BFTW *ftwbuf = state->ftwbuf;
	return fnmatch(expr->argv[1], ftwbuf->path, expr->num) == 0;
}

/**
 * -perm test.
 */
bool eval_perm(const struct bfs_expr *expr, struct bfs_eval *state) {
	const struct bfs_stat *statbuf = eval_stat(state);
	if (!statbuf) {
		return false;
	}

	mode_t mode = statbuf->mode;
	mode_t target;
	if (state->ftwbuf->type == BFS_DIR) {
		target = expr->dir_mode;
	} else {
		target = expr->file_mode;
	}

	switch (expr->mode_cmp) {
	case BFS_MODE_EQUAL:
		return (mode & 07777) == target;

	case BFS_MODE_ALL:
		return (mode & target) == target;

	case BFS_MODE_ANY:
		return !(mode & target) == !target;
	}

	bfs_bug("Invalid comparison mode");
	return false;
}

/** Print a user/group name/id, and update the column width. */
static int print_owner(FILE *file, const char *name, uintmax_t id, int *width) {
	if (name) {
		int len = xstrwidth(name);
		if (*width < len) {
			*width = len;
		}

		return fprintf(file, " %s%*s", name, *width - len, "");
	} else {
		int ret = fprintf(file, " %-*ju", *width, id);
		if (ret >= 0 && *width < ret - 1) {
			*width = ret - 1;
		}
		return ret;
	}
}

/**
 * -f?ls action.
 */
bool eval_fls(const struct bfs_expr *expr, struct bfs_eval *state) {
	CFILE *cfile = expr->cfile;
	FILE *file = cfile->file;
	const struct bfs_ctx *ctx = state->ctx;
	const struct BFTW *ftwbuf = state->ftwbuf;
	const struct bfs_stat *statbuf = eval_stat(state);
	if (!statbuf) {
		goto done;
	}

	// ls -l prints non-path text in the "normal" color, so do the same
	if (cfprintf(cfile, "${no}") < 0) {
		goto error;
	}

	uintmax_t ino = statbuf->ino;
	uintmax_t block_size = ctx->posixly_correct ? 512 : 1024;
	uintmax_t blocks = ((uintmax_t)statbuf->blocks*BFS_STAT_BLKSIZE + block_size - 1)/block_size;
	char mode[11];
	xstrmode(statbuf->mode, mode);
	char acl = bfs_check_acl(ftwbuf) > 0 ? '+' : ' ';
	uintmax_t nlink = statbuf->nlink;
	if (fprintf(file, "%9ju %6ju %s%c %2ju", ino, blocks, mode, acl, nlink) < 0) {
		goto error;
	}

	const struct passwd *pwd = bfs_getpwuid(ctx->users, statbuf->uid);
	static int uwidth = 8;
	if (print_owner(file, pwd ? pwd->pw_name : NULL, statbuf->uid, &uwidth) < 0) {
		goto error;
	}

	const struct group *grp = bfs_getgrgid(ctx->groups, statbuf->gid);
	static int gwidth = 8;
	if (print_owner(file, grp ? grp->gr_name : NULL, statbuf->gid, &gwidth) < 0) {
		goto error;
	}

	if (ftwbuf->type == BFS_BLK || ftwbuf->type == BFS_CHR) {
		int ma = xmajor(statbuf->rdev);
		int mi = xminor(statbuf->rdev);
		if (fprintf(file, " %3d, %3d", ma, mi) < 0) {
			goto error;
		}
	} else {
		uintmax_t size = statbuf->size;
		if (fprintf(file, " %8ju", size) < 0) {
			goto error;
		}
	}

	time_t time = statbuf->mtime.tv_sec;
	time_t now = ctx->now.tv_sec;
	time_t six_months_ago = now - 6*30*24*60*60;
	time_t tomorrow = now + 24*60*60;
	struct tm tm;
	if (xlocaltime(&time, &tm) != 0) {
		goto error;
	}
	char time_str[256];
	size_t time_ret;
	if (time <= six_months_ago || time >= tomorrow) {
		time_ret = strftime(time_str, sizeof(time_str), "%b %e  %Y", &tm);
	} else {
		time_ret = strftime(time_str, sizeof(time_str), "%b %e %H:%M", &tm);
	}
	if (time_ret == 0) {
		errno = EOVERFLOW;
		goto error;
	}
	if (cfprintf(cfile, " %s${rs}", time_str) < 0) {
		goto error;
	}

	if (cfprintf(cfile, " %pP", ftwbuf) < 0) {
		goto error;
	}

	if (ftwbuf->type == BFS_LNK) {
		if (cfprintf(cfile, " -> %pL", ftwbuf) < 0) {
			goto error;
		}
	}

	if (fputc('\n', file) == EOF) {
		goto error;
	}

done:
	return true;

error:
	eval_io_error(expr, state);
	return true;
}

/**
 * -f?print action.
 */
bool eval_fprint(const struct bfs_expr *expr, struct bfs_eval *state) {
	if (cfprintf(expr->cfile, "%pP\n", state->ftwbuf) < 0) {
		eval_io_error(expr, state);
	}
	return true;
}

/**
 * -f?print0 action.
 */
bool eval_fprint0(const struct bfs_expr *expr, struct bfs_eval *state) {
	const char *path = state->ftwbuf->path;
	size_t length = strlen(path) + 1;
	if (fwrite(path, 1, length, expr->cfile->file) != length) {
		eval_io_error(expr, state);
	}
	return true;
}

/**
 * -f?printf action.
 */
bool eval_fprintf(const struct bfs_expr *expr, struct bfs_eval *state) {
	if (bfs_printf(expr->cfile, expr->printf, state->ftwbuf) != 0) {
		eval_io_error(expr, state);
	}

	return true;
}

/**
 * -printx action.
 */
bool eval_fprintx(const struct bfs_expr *expr, struct bfs_eval *state) {
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
	eval_io_error(expr, state);
	return true;
}

/**
 * -prune action.
 */
bool eval_prune(const struct bfs_expr *expr, struct bfs_eval *state) {
	state->action = BFTW_PRUNE;
	return true;
}

/**
 * -quit action.
 */
bool eval_quit(const struct bfs_expr *expr, struct bfs_eval *state) {
	state->action = BFTW_STOP;
	state->quit = true;
	return true;
}

/**
 * -i?regex test.
 */
bool eval_regex(const struct bfs_expr *expr, struct bfs_eval *state) {
	const char *path = state->ftwbuf->path;

	int ret = bfs_regexec(expr->regex, path, BFS_REGEX_ANCHOR);
	if (ret < 0) {
		char *str = bfs_regerror(expr->regex);
		if (str) {
			eval_error(state, "%s.\n", str);
			free(str);
		} else {
			eval_error(state, "bfs_regerror(): %m.\n");
		}
	}

	return ret > 0;
}

/**
 * -samefile test.
 */
bool eval_samefile(const struct bfs_expr *expr, struct bfs_eval *state) {
	const struct bfs_stat *statbuf = eval_stat(state);
	if (!statbuf) {
		return false;
	}

	return statbuf->dev == expr->dev && statbuf->ino == expr->ino;
}

/**
 * -size test.
 */
bool eval_size(const struct bfs_expr *expr, struct bfs_eval *state) {
	const struct bfs_stat *statbuf = eval_stat(state);
	if (!statbuf) {
		return false;
	}

	static const off_t scales[] = {
		[BFS_BLOCKS] = 512,
		[BFS_BYTES] = 1,
		[BFS_WORDS] = 2,
		[BFS_KB] = 1LL << 10,
		[BFS_MB] = 1LL << 20,
		[BFS_GB] = 1LL << 30,
		[BFS_TB] = 1LL << 40,
		[BFS_PB] = 1LL << 50,
	};

	off_t scale = scales[expr->size_unit];
	off_t size = (statbuf->size + scale - 1)/scale; // Round up
	return bfs_expr_cmp(expr, size);
}

/**
 * -sparse test.
 */
bool eval_sparse(const struct bfs_expr *expr, struct bfs_eval *state) {
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
bool eval_type(const struct bfs_expr *expr, struct bfs_eval *state) {
	return (1 << state->ftwbuf->type) & expr->num;
}

/**
 * -xattr test.
 */
bool eval_xattr(const struct bfs_expr *expr, struct bfs_eval *state) {
	int ret = bfs_check_xattrs(state->ftwbuf);
	if (ret >= 0) {
		return ret;
	} else {
		eval_report_error(state);
		return false;
	}
}

/**
 * -xattrname test.
 */
bool eval_xattrname(const struct bfs_expr *expr, struct bfs_eval *state) {
	int ret = bfs_check_xattr_named(state->ftwbuf, expr->argv[1]);
	if (ret >= 0) {
		return ret;
	} else {
		eval_report_error(state);
		return false;
	}
}

/**
 * -xtype test.
 */
bool eval_xtype(const struct bfs_expr *expr, struct bfs_eval *state) {
	const struct BFTW *ftwbuf = state->ftwbuf;
	enum bfs_stat_flags flags = ftwbuf->stat_flags ^ (BFS_STAT_NOFOLLOW | BFS_STAT_TRYFOLLOW);
	enum bfs_type type = bftw_type(ftwbuf, flags);
	if (type == BFS_ERROR) {
		eval_report_error(state);
		return false;
	} else {
		return (1 << type) & expr->num;
	}
}

#if _POSIX_MONOTONIC_CLOCK > 0
#  define BFS_CLOCK CLOCK_MONOTONIC
#elif _POSIX_TIMERS > 0
#  define BFS_CLOCK CLOCK_REALTIME
#endif

/**
 * Call clock_gettime(), if available.
 */
static int eval_gettime(struct bfs_eval *state, struct timespec *ts) {
#ifdef BFS_CLOCK
	int ret = clock_gettime(BFS_CLOCK, ts);
	if (ret != 0) {
		bfs_warning(state->ctx, "%pP: clock_gettime(): %m.\n", state->ftwbuf);
	}
	return ret;
#else
	return -1;
#endif
}

/**
 * Record an elapsed time.
 */
static void timespec_elapsed(struct timespec *elapsed, const struct timespec *start, const struct timespec *end) {
	elapsed->tv_sec += end->tv_sec - start->tv_sec;
	elapsed->tv_nsec += end->tv_nsec - start->tv_nsec;
	if (elapsed->tv_nsec < 0) {
		elapsed->tv_nsec += 1000000000L;
		--elapsed->tv_sec;
	} else if (elapsed->tv_nsec >= 1000000000L) {
		elapsed->tv_nsec -= 1000000000L;
		++elapsed->tv_sec;
	}
}

/**
 * Evaluate an expression.
 */
static bool eval_expr(struct bfs_expr *expr, struct bfs_eval *state) {
	struct timespec start, end;
	bool time = state->ctx->debug & DEBUG_RATES;
	if (time) {
		if (eval_gettime(state, &start) != 0) {
			time = false;
		}
	}

	bfs_assert(!state->quit);

	bool ret = expr->eval_fn(expr, state);

	if (time) {
		if (eval_gettime(state, &end) == 0) {
			timespec_elapsed(&expr->elapsed, &start, &end);
		}
	}

	++expr->evaluations;
	if (ret) {
		++expr->successes;
	}

	if (bfs_expr_never_returns(expr)) {
		bfs_assert(state->quit);
	} else if (!state->quit) {
		bfs_assert(!expr->always_true || ret);
		bfs_assert(!expr->always_false || !ret);
	}

	return ret;
}

/**
 * Evaluate a negation.
 */
bool eval_not(const struct bfs_expr *expr, struct bfs_eval *state) {
	return !eval_expr(expr->rhs, state);
}

/**
 * Evaluate a conjunction.
 */
bool eval_and(const struct bfs_expr *expr, struct bfs_eval *state) {
	if (!eval_expr(expr->lhs, state)) {
		return false;
	}

	if (state->quit) {
		return false;
	}

	return eval_expr(expr->rhs, state);
}

/**
 * Evaluate a disjunction.
 */
bool eval_or(const struct bfs_expr *expr, struct bfs_eval *state) {
	if (eval_expr(expr->lhs, state)) {
		return true;
	}

	if (state->quit) {
		return false;
	}

	return eval_expr(expr->rhs, state);
}

/**
 * Evaluate the comma operator.
 */
bool eval_comma(const struct bfs_expr *expr, struct bfs_eval *state) {
	eval_expr(expr->lhs, state);

	if (state->quit) {
		return false;
	}

	return eval_expr(expr->rhs, state);
}

/** Update the status bar. */
static void eval_status(struct bfs_eval *state, struct bfs_bar *bar, struct timespec *last_status, size_t count) {
	struct timespec now;
	if (eval_gettime(state, &now) == 0) {
		struct timespec elapsed = {0};
		timespec_elapsed(&elapsed, last_status, &now);

		// Update every 0.1s
		if (elapsed.tv_sec > 0 || elapsed.tv_nsec >= 100000000L) {
			*last_status = now;
		} else {
			return;
		}
	}

	size_t width = bfs_bar_width(bar);
	if (width < 3) {
		return;
	}

	const struct BFTW *ftwbuf = state->ftwbuf;

	char *rhs = dstrprintf(" (visited: %zu, depth: %2zu)", count, ftwbuf->depth);
	if (!rhs) {
		return;
	}

	size_t rhslen = dstrlen(rhs);
	if (3 + rhslen > width) {
		dstresize(&rhs, 0);
		rhslen = 0;
	}

	char *status = dstralloc(0);
	if (!status) {
		goto out_rhs;
	}

	const char *path = ftwbuf->path;
	size_t pathlen = ftwbuf->nameoff;
	if (ftwbuf->depth == 0) {
		pathlen = strlen(path);
	}

	// Try to make sure even wide characters fit in the status bar
	size_t pathmax = width - rhslen - 3;
	size_t pathwidth = 0;
	mbstate_t mb;
	memset(&mb, 0, sizeof(mb));
	while (pathlen > 0) {
		wchar_t wc;
		size_t len = mbrtowc(&wc, path, pathlen, &mb);
		int cwidth;
		if (len == (size_t)-1) {
			// Invalid byte sequence, assume a single-width '?'
			len = 1;
			cwidth = 1;
			memset(&mb, 0, sizeof(mb));
		} else if (len == (size_t)-2) {
			// Incomplete byte sequence, assume a single-width '?'
			len = pathlen;
			cwidth = 1;
		} else {
			cwidth = wcwidth(wc);
			if (cwidth < 0) {
				cwidth = 0;
			}
		}

		if (pathwidth + cwidth > pathmax) {
			break;
		}

		if (dstrncat(&status, path, len) != 0) {
			goto out_rhs;
		}

		path += len;
		pathlen -= len;
		pathwidth += cwidth;
	}

	if (dstrcat(&status, "...") != 0) {
		goto out_rhs;
	}

	while (pathwidth < pathmax) {
		if (dstrapp(&status, ' ') != 0) {
			goto out_rhs;
		}
		++pathwidth;
	}

	if (dstrcat(&status, rhs) != 0) {
		goto out_rhs;
	}

	bfs_bar_update(bar, status);

	dstrfree(status);
out_rhs:
	dstrfree(rhs);
}

/** Check if we've seen a file before. */
static bool eval_file_unique(struct bfs_eval *state, struct trie *seen) {
	const struct bfs_stat *statbuf = eval_stat(state);
	if (!statbuf) {
		return false;
	}

	bfs_file_id id;
	bfs_stat_id(statbuf, &id);

	struct trie_leaf *leaf = trie_insert_mem(seen, id, sizeof(id));
	if (!leaf) {
		eval_report_error(state);
		return false;
	}

	if (leaf->value) {
		state->action = BFTW_PRUNE;
		return false;
	} else {
		leaf->value = leaf;
		return true;
	}
}

#define DEBUG_FLAG(flags, flag) \
	do { \
		if ((flags & flag) || flags == flag) { \
			fputs(#flag, stderr); \
			flags ^= flag; \
			if (flags) { \
				fputs(" | ", stderr); \
			} \
		} \
	} while (0)

/**
 * Log a stat() call.
 */
static void debug_stat(const struct bfs_ctx *ctx, const struct BFTW *ftwbuf, const struct bftw_stat *cache, enum bfs_stat_flags flags) {
	bfs_debug_prefix(ctx, DEBUG_STAT);

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

	DEBUG_FLAG(flags, BFS_STAT_FOLLOW);
	DEBUG_FLAG(flags, BFS_STAT_NOFOLLOW);
	DEBUG_FLAG(flags, BFS_STAT_TRYFOLLOW);
	DEBUG_FLAG(flags, BFS_STAT_NOSYNC);

	fprintf(stderr, ") == %d", cache->buf ? 0 : -1);

	if (cache->error) {
		fprintf(stderr, " [%d]", cache->error);
	}

	fprintf(stderr, "\n");
}

/**
 * Log any stat() calls that happened.
 */
static void debug_stats(const struct bfs_ctx *ctx, const struct BFTW *ftwbuf) {
	if (!(ctx->debug & DEBUG_STAT)) {
		return;
	}

	const struct bfs_stat *statbuf = ftwbuf->stat_cache.buf;
	if (statbuf || ftwbuf->stat_cache.error) {
		debug_stat(ctx, ftwbuf, &ftwbuf->stat_cache, BFS_STAT_FOLLOW);
	}

	const struct bfs_stat *lstatbuf = ftwbuf->lstat_cache.buf;
	if ((lstatbuf && lstatbuf != statbuf) || ftwbuf->lstat_cache.error) {
		debug_stat(ctx, ftwbuf, &ftwbuf->lstat_cache, BFS_STAT_NOFOLLOW);
	}
}

#define DUMP_MAP(value) [value] = #value

/**
 * Dump the bfs_type for -D search.
 */
static const char *dump_bfs_type(enum bfs_type type) {
	static const char *types[] = {
		DUMP_MAP(BFS_UNKNOWN),
		DUMP_MAP(BFS_BLK),
		DUMP_MAP(BFS_CHR),
		DUMP_MAP(BFS_DIR),
		DUMP_MAP(BFS_DOOR),
		DUMP_MAP(BFS_FIFO),
		DUMP_MAP(BFS_LNK),
		DUMP_MAP(BFS_PORT),
		DUMP_MAP(BFS_REG),
		DUMP_MAP(BFS_SOCK),
		DUMP_MAP(BFS_WHT),
	};

	if (type == BFS_ERROR) {
		return "BFS_ERROR";
	} else {
		return types[type];
	}
}

/**
 * Dump the bftw_visit for -D search.
 */
static const char *dump_bftw_visit(enum bftw_visit visit) {
	static const char *visits[] = {
		DUMP_MAP(BFTW_PRE),
		DUMP_MAP(BFTW_POST),
	};
	return visits[visit];
}

/**
 * Dump the bftw_action for -D search.
 */
static const char *dump_bftw_action(enum bftw_action action) {
	static const char *actions[] = {
		DUMP_MAP(BFTW_CONTINUE),
		DUMP_MAP(BFTW_PRUNE),
		DUMP_MAP(BFTW_STOP),
	};
	return actions[action];
}

/**
 * Type passed as the argument to the bftw() callback.
 */
struct callback_args {
	/** The bfs context. */
	const struct bfs_ctx *ctx;

	/** The status bar. */
	struct bfs_bar *bar;
	/** The time of the last status update. */
	struct timespec last_status;
	/** The number of files visited so far. */
	size_t count;

	/** The set of seen files. */
	struct trie *seen;

	/** Eventual return value from bfs_eval(). */
	int ret;
};

/**
 * bftw() callback.
 */
static enum bftw_action eval_callback(const struct BFTW *ftwbuf, void *ptr) {
	struct callback_args *args = ptr;
	++args->count;

	const struct bfs_ctx *ctx = args->ctx;

	struct bfs_eval state;
	state.ftwbuf = ftwbuf;
	state.ctx = ctx;
	state.action = BFTW_CONTINUE;
	state.ret = &args->ret;
	state.quit = false;

	if (args->bar) {
		eval_status(&state, args->bar, &args->last_status, args->count);
	}

	if (ftwbuf->type == BFS_ERROR) {
		if (!eval_should_ignore(&state, ftwbuf->error)) {
			eval_error(&state, "%s.\n", strerror(ftwbuf->error));
		}
		state.action = BFTW_PRUNE;
		goto done;
	}

	if (ctx->unique && ftwbuf->visit == BFTW_PRE) {
		if (!eval_file_unique(&state, args->seen)) {
			goto done;
		}
	}

	if (eval_expr(ctx->exclude, &state)) {
		state.action = BFTW_PRUNE;
		goto done;
	}

	if (ctx->xargs_safe && strpbrk(ftwbuf->path, " \t\n\'\"\\")) {
		eval_error(&state, "Path is not safe for xargs.\n");
		state.action = BFTW_PRUNE;
		goto done;
	}

	if (ctx->maxdepth < 0 || ftwbuf->depth >= (size_t)ctx->maxdepth) {
		state.action = BFTW_PRUNE;
	}

	// In -depth mode, only handle directories on the BFTW_POST visit
	enum bftw_visit expected_visit = BFTW_PRE;
	if ((ctx->flags & BFTW_POST_ORDER)
	    && (ctx->strategy == BFTW_IDS || ftwbuf->type == BFS_DIR)
	    && ftwbuf->depth < (size_t)ctx->maxdepth) {
		expected_visit = BFTW_POST;
	}

	if (ftwbuf->visit == expected_visit
	    && ftwbuf->depth >= (size_t)ctx->mindepth
	    && ftwbuf->depth <= (size_t)ctx->maxdepth) {
		eval_expr(ctx->expr, &state);
	}

done:
	debug_stats(ctx, ftwbuf);

	if (bfs_debug(ctx, DEBUG_SEARCH, "eval_callback({\n")) {
		fprintf(stderr, "\t.path = \"%s\",\n", ftwbuf->path);
		fprintf(stderr, "\t.root = \"%s\",\n", ftwbuf->root);
		fprintf(stderr, "\t.depth = %zu,\n", ftwbuf->depth);
		fprintf(stderr, "\t.visit = %s,\n", dump_bftw_visit(ftwbuf->visit));
		fprintf(stderr, "\t.type = %s,\n", dump_bfs_type(ftwbuf->type));
		fprintf(stderr, "\t.error = %d,\n", ftwbuf->error);
		fprintf(stderr, "}) == %s\n", dump_bftw_action(state.action));
	}

	return state.action;
}

/** Check if an rlimit value is infinite. */
static bool rlim_isinf(rlim_t r) {
	// Consider RLIM_{INFINITY,SAVED_{CUR,MAX}} all equally infinite
	if (r == RLIM_INFINITY) {
		return true;
	}

#ifdef RLIM_SAVED_CUR
	if (r == RLIM_SAVED_CUR) {
		return true;
	}
#endif

#ifdef RLIM_SAVED_MAX
	if (r == RLIM_SAVED_MAX) {
		return true;
	}
#endif

	return false;
}

/** Compare two rlimit values, accounting for RLIM_INFINITY etc. */
static int rlim_cmp(rlim_t a, rlim_t b) {
	bool a_inf = rlim_isinf(a);
	bool b_inf = rlim_isinf(b);
	if (a_inf || b_inf) {
		return a_inf - b_inf;
	}

	return (a > b) - (a < b);
}

/** Raise RLIMIT_NOFILE if possible, and return the new limit. */
static int raise_fdlimit(const struct bfs_ctx *ctx) {
	rlim_t target = 64 << 10;
	if (rlim_cmp(target, ctx->nofile_hard) > 0) {
		target = ctx->nofile_hard;
	}

	int ret = target;

	if (rlim_cmp(target, ctx->nofile_soft) > 0) {
		const struct rlimit rl = {
			.rlim_cur = target,
			.rlim_max = ctx->nofile_hard,
		};
		if (setrlimit(RLIMIT_NOFILE, &rl) != 0) {
			ret = ctx->nofile_soft;
		}
	}

	return ret;
}

/** Preallocate the fd table in the kernel. */
static void reserve_fds(int limit) {
	// Kernels typically implement the fd table as a dynamic array.
	// Growing the array can be expensive, especially if files are being
	// opened in parallel.  We can work around this by allocating the
	// highest possible fd, forcing the kernel to grow the table upfront.

#ifdef F_DUPFD_CLOEXEC
	int fd = fcntl(STDIN_FILENO, F_DUPFD_CLOEXEC, limit - 1);
#else
	int fd = fcntl(STDIN_FILENO, F_DUPFD, limit - 1);
#endif
	if (fd >= 0) {
		xclose(fd);
	}
}

/** Infer the number of file descriptors available to bftw(). */
static int infer_fdlimit(const struct bfs_ctx *ctx, int limit) {
	// 3 for std{in,out,err}
	int nopen = 3 + ctx->nfiles;

	// Check /proc/self/fd for the current number of open fds, if possible
	// (we may have inherited more than just the standard ones)
	struct bfs_dir *dir = bfs_allocdir();
	if (!dir) {
		goto done;
	}

	if (bfs_opendir(dir, AT_FDCWD, "/proc/self/fd") != 0
	    && bfs_opendir(dir, AT_FDCWD, "/dev/fd") != 0) {
		goto done;
	}

	// Account for 'dir' itself
	nopen = -1;

	while (bfs_readdir(dir, NULL) > 0) {
		++nopen;
	}
	bfs_closedir(dir);
done:
	free(dir);

	int ret = limit - nopen;
	ret -= ctx->expr->persistent_fds;
	ret -= ctx->expr->ephemeral_fds;

	// bftw() needs at least 2 available fds
	if (ret < 2) {
		ret = 2;
	}

	return ret;
}

static int infer_nproc(void) {
	long nproc = sysconf(_SC_NPROCESSORS_ONLN);

	if (nproc < 0) {
		nproc = 0;
	} else if (nproc > 8) {
		// Not much speedup after 8 threads
		nproc = 8;
	}

	return nproc;
}

/**
 * Dump the bftw() flags for -D search.
 */
static void dump_bftw_flags(enum bftw_flags flags) {
	DEBUG_FLAG(flags, 0);
	DEBUG_FLAG(flags, BFTW_STAT);
	DEBUG_FLAG(flags, BFTW_RECOVER);
	DEBUG_FLAG(flags, BFTW_POST_ORDER);
	DEBUG_FLAG(flags, BFTW_FOLLOW_ROOTS);
	DEBUG_FLAG(flags, BFTW_FOLLOW_ALL);
	DEBUG_FLAG(flags, BFTW_DETECT_CYCLES);
	DEBUG_FLAG(flags, BFTW_SKIP_MOUNTS);
	DEBUG_FLAG(flags, BFTW_PRUNE_MOUNTS);
	DEBUG_FLAG(flags, BFTW_SORT);
	DEBUG_FLAG(flags, BFTW_BUFFER);

	bfs_assert(flags == 0, "Missing bftw flag 0x%X", flags);
}

/**
 * Dump the bftw_strategy for -D search.
 */
static const char *dump_bftw_strategy(enum bftw_strategy strategy) {
	static const char *strategies[] = {
		DUMP_MAP(BFTW_BFS),
		DUMP_MAP(BFTW_DFS),
		DUMP_MAP(BFTW_IDS),
		DUMP_MAP(BFTW_EDS),
	};
	return strategies[strategy];
}

/** Check if we need to enable BFTW_BUFFER. */
static bool eval_must_buffer(const struct bfs_expr *expr) {
#if __FreeBSD__
	// FreeBSD doesn't properly handle adding/removing directory entries
	// during readdir() on NFS mounts.  Work around it by passing BFTW_BUFFER
	// whenever we could be mutating the directory ourselves through -delete
	// or -exec.  We don't attempt to handle concurrent modification by other
	// processes, which are racey anyway.
	//
	// https://bugs.freebsd.org/bugzilla/show_bug.cgi?id=57696
	// https://github.com/tavianator/bfs/issues/67

	if (expr->eval_fn == eval_delete || expr->eval_fn == eval_exec) {
		return true;
	}

	if (bfs_expr_is_parent(expr)) {
		if (expr->lhs && eval_must_buffer(expr->lhs)) {
			return true;
		}

		if (expr->rhs && eval_must_buffer(expr->rhs)) {
			return true;
		}
	}
#endif // __FreeBSD__

	return false;
}

int bfs_eval(const struct bfs_ctx *ctx) {
	if (!ctx->expr) {
		return EXIT_SUCCESS;
	}

	struct callback_args args = {
		.ctx = ctx,
		.ret = EXIT_SUCCESS,
	};

	if (ctx->status) {
		args.bar = bfs_bar_show();
		if (!args.bar) {
			bfs_warning(ctx, "Couldn't show status bar: %m.\n\n");
		}
	}

	struct trie seen;
	if (ctx->unique) {
		trie_init(&seen);
		args.seen = &seen;
	}

	int fdlimit = raise_fdlimit(ctx);
	reserve_fds(fdlimit);
	fdlimit = infer_fdlimit(ctx, fdlimit);

	int nthreads;
	if (ctx->threads > 0) {
		nthreads = ctx->threads - 1;
	} else {
		nthreads = infer_nproc();
	}

	struct bftw_args bftw_args = {
		.paths = ctx->paths,
		.npaths = darray_length(ctx->paths),
		.callback = eval_callback,
		.ptr = &args,
		.nopenfd = fdlimit,
		.nthreads = nthreads,
		.flags = ctx->flags,
		.strategy = ctx->strategy,
		.mtab = bfs_ctx_mtab(ctx),
	};

	if (eval_must_buffer(ctx->expr)) {
		bftw_args.flags |= BFTW_BUFFER;
	}

	if (bfs_debug(ctx, DEBUG_SEARCH, "bftw({\n")) {
		fprintf(stderr, "\t.paths = {\n");
		for (size_t i = 0; i < bftw_args.npaths; ++i) {
			fprintf(stderr, "\t\t\"%s\",\n", bftw_args.paths[i]);
		}
		fprintf(stderr, "\t},\n");
		fprintf(stderr, "\t.npaths = %zu,\n", bftw_args.npaths);
		fprintf(stderr, "\t.callback = eval_callback,\n");
		fprintf(stderr, "\t.ptr = &args,\n");
		fprintf(stderr, "\t.nopenfd = %d,\n", bftw_args.nopenfd);
		fprintf(stderr, "\t.nthreads = %d,\n", bftw_args.nthreads);
		fprintf(stderr, "\t.flags = ");
		dump_bftw_flags(bftw_args.flags);
		fprintf(stderr, ",\n\t.strategy = %s,\n", dump_bftw_strategy(bftw_args.strategy));
		fprintf(stderr, "\t.mtab = ");
		if (bftw_args.mtab) {
			fprintf(stderr, "ctx->mtab");
		} else {
			fprintf(stderr, "NULL");
		}
		fprintf(stderr, ",\n})\n");
	}

	if (bftw(&bftw_args) != 0) {
		args.ret = EXIT_FAILURE;
		bfs_perror(ctx, "bftw()");
	}

	if (eval_exec_finish(ctx->expr, ctx) != 0) {
		args.ret = EXIT_FAILURE;
	}

	bfs_ctx_dump(ctx, DEBUG_RATES);

	if (ctx->unique) {
		trie_destroy(&seen);
	}

	bfs_bar_hide(args.bar);

	return args.ret;
}
