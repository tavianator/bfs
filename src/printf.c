// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include "printf.h"
#include "alloc.h"
#include "bfstd.h"
#include "bftw.h"
#include "color.h"
#include "config.h"
#include "ctx.h"
#include "diag.h"
#include "dir.h"
#include "dstring.h"
#include "expr.h"
#include "mtab.h"
#include "pwcache.h"
#include "stat.h"
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct bfs_fmt;

/**
 * A function implementing a printf directive.
 */
typedef int bfs_printf_fn(CFILE *cfile, const struct bfs_fmt *fmt, const struct BFTW *ftwbuf);

/**
 * A single formatting directive like %f or %#4m.
 */
struct bfs_fmt {
	/** The printing function to invoke. */
	bfs_printf_fn *fn;
	/** String data associated with this directive. */
	dchar *str;
	/** The stat field to print. */
	enum bfs_stat_field stat_field;
	/** Character data associated with this directive. */
	char c;
	/** Some data used by the directive. */
	void *ptr;
};

/**
 * An entire format string.
 */
struct bfs_printf {
	/** An array of formatting directives. */
	struct bfs_fmt *fmts;
	/** The number of directives. */
	size_t nfmts;
};

/** Print some text as-is. */
static int bfs_printf_literal(CFILE *cfile, const struct bfs_fmt *fmt, const struct BFTW *ftwbuf) {
	size_t len = dstrlen(fmt->str);
	if (fwrite(fmt->str, 1, len, cfile->file) == len) {
		return 0;
	} else {
		return -1;
	}
}

/** \c: flush */
static int bfs_printf_flush(CFILE *cfile, const struct bfs_fmt *fmt, const struct BFTW *ftwbuf) {
	return fflush(cfile->file);
}

/** Check if we can safely colorize this directive. */
static bool should_color(CFILE *cfile, const struct bfs_fmt *fmt) {
	return cfile->colors && strcmp(fmt->str, "%s") == 0;
}

/**
 * Print a value to a temporary buffer before formatting it.
 */
#define BFS_PRINTF_BUF(buf, format, ...) \
	char buf[256]; \
	int ret = snprintf(buf, sizeof(buf), format, __VA_ARGS__); \
	bfs_assert(ret >= 0 && (size_t)ret < sizeof(buf)); \
	(void)ret

/**
 * Common entry point for fprintf() with a dynamic format string.
 */
static int dyn_fprintf(FILE *file, const struct bfs_fmt *fmt, ...) {
	va_list args;
	va_start(args, fmt);

#if __GNUC__
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif
	int ret = vfprintf(file, fmt->str, args);
#if __GNUC__
#  pragma GCC diagnostic pop
#endif

	va_end(args);
	return ret;
}

/** %a, %c, %t: ctime() */
static int bfs_printf_ctime(CFILE *cfile, const struct bfs_fmt *fmt, const struct BFTW *ftwbuf) {
	// Not using ctime() itself because GNU find adds nanoseconds
	static const char *days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
	static const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

	const struct bfs_stat *statbuf = bftw_stat(ftwbuf, ftwbuf->stat_flags);
	if (!statbuf) {
		return -1;
	}

	const struct timespec *ts = bfs_stat_time(statbuf, fmt->stat_field);
	if (!ts) {
		return -1;
	}

	struct tm tm;
	if (!localtime_r(&ts->tv_sec, &tm)) {
		return -1;
	}

	BFS_PRINTF_BUF(buf, "%s %s %2d %.2d:%.2d:%.2d.%09ld0 %4d",
		days[tm.tm_wday],
		months[tm.tm_mon],
		tm.tm_mday,
		tm.tm_hour,
		tm.tm_min,
		tm.tm_sec,
		(long)ts->tv_nsec,
		1900 + tm.tm_year);

	return dyn_fprintf(cfile->file, fmt, buf);
}

/** %A, %B/%W, %C, %T: strftime() */
static int bfs_printf_strftime(CFILE *cfile, const struct bfs_fmt *fmt, const struct BFTW *ftwbuf) {
	const struct bfs_stat *statbuf = bftw_stat(ftwbuf, ftwbuf->stat_flags);
	if (!statbuf) {
		return -1;
	}

	const struct timespec *ts = bfs_stat_time(statbuf, fmt->stat_field);
	if (!ts) {
		return -1;
	}

	struct tm tm;
	if (!localtime_r(&ts->tv_sec, &tm)) {
		return -1;
	}

	int ret;
	char buf[256];
	char format[] = "% ";
	switch (fmt->c) {
	// Non-POSIX strftime() features
	case '@':
		ret = snprintf(buf, sizeof(buf), "%lld.%09ld0", (long long)ts->tv_sec, (long)ts->tv_nsec);
		break;
	case '+':
		ret = snprintf(buf, sizeof(buf), "%4d-%.2d-%.2d+%.2d:%.2d:%.2d.%09ld0",
			1900 + tm.tm_year,
			tm.tm_mon + 1,
			tm.tm_mday,
			tm.tm_hour,
			tm.tm_min,
			tm.tm_sec,
			(long)ts->tv_nsec);
		break;
	case 'k':
		ret = snprintf(buf, sizeof(buf), "%2d", tm.tm_hour);
		break;
	case 'l':
		ret = snprintf(buf, sizeof(buf), "%2d", (tm.tm_hour + 11) % 12 + 1);
		break;
	case 's':
		ret = snprintf(buf, sizeof(buf), "%lld", (long long)ts->tv_sec);
		break;
	case 'S':
		ret = snprintf(buf, sizeof(buf), "%.2d.%09ld0", tm.tm_sec, (long)ts->tv_nsec);
		break;
	case 'T':
		ret = snprintf(buf, sizeof(buf), "%.2d:%.2d:%.2d.%09ld0",
			tm.tm_hour,
			tm.tm_min,
			tm.tm_sec,
			(long)ts->tv_nsec);
		break;

	// POSIX strftime() features
	default:
		format[1] = fmt->c;
#if __GNUC__
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif
		ret = strftime(buf, sizeof(buf), format, &tm);
#if __GNUC__
#  pragma GCC diagnostic pop
#endif
		break;
	}

	bfs_assert(ret >= 0 && (size_t)ret < sizeof(buf));
	(void)ret;

	return dyn_fprintf(cfile->file, fmt, buf);
}

/** %b: blocks */
static int bfs_printf_b(CFILE *cfile, const struct bfs_fmt *fmt, const struct BFTW *ftwbuf) {
	const struct bfs_stat *statbuf = bftw_stat(ftwbuf, ftwbuf->stat_flags);
	if (!statbuf) {
		return -1;
	}

	uintmax_t blocks = ((uintmax_t)statbuf->blocks * BFS_STAT_BLKSIZE + 511) / 512;
	BFS_PRINTF_BUF(buf, "%ju", blocks);
	return dyn_fprintf(cfile->file, fmt, buf);
}

/** %d: depth */
static int bfs_printf_d(CFILE *cfile, const struct bfs_fmt *fmt, const struct BFTW *ftwbuf) {
	return dyn_fprintf(cfile->file, fmt, (intmax_t)ftwbuf->depth);
}

/** %D: device */
static int bfs_printf_D(CFILE *cfile, const struct bfs_fmt *fmt, const struct BFTW *ftwbuf) {
	const struct bfs_stat *statbuf = bftw_stat(ftwbuf, ftwbuf->stat_flags);
	if (!statbuf) {
		return -1;
	}

	BFS_PRINTF_BUF(buf, "%ju", (uintmax_t)statbuf->dev);
	return dyn_fprintf(cfile->file, fmt, buf);
}

/** %f: file name */
static int bfs_printf_f(CFILE *cfile, const struct bfs_fmt *fmt, const struct BFTW *ftwbuf) {
	if (should_color(cfile, fmt)) {
		return cfprintf(cfile, "%pF", ftwbuf);
	} else {
		return dyn_fprintf(cfile->file, fmt, ftwbuf->path + ftwbuf->nameoff);
	}
}

/** %F: file system type */
static int bfs_printf_F(CFILE *cfile, const struct bfs_fmt *fmt, const struct BFTW *ftwbuf) {
	const struct bfs_stat *statbuf = bftw_stat(ftwbuf, ftwbuf->stat_flags);
	if (!statbuf) {
		return -1;
	}

	const char *type = bfs_fstype(fmt->ptr, statbuf);
	if (!type) {
		return -1;
	}

	return dyn_fprintf(cfile->file, fmt, type);
}

/** %G: gid */
static int bfs_printf_G(CFILE *cfile, const struct bfs_fmt *fmt, const struct BFTW *ftwbuf) {
	const struct bfs_stat *statbuf = bftw_stat(ftwbuf, ftwbuf->stat_flags);
	if (!statbuf) {
		return -1;
	}

	BFS_PRINTF_BUF(buf, "%ju", (uintmax_t)statbuf->gid);
	return dyn_fprintf(cfile->file, fmt, buf);
}

/** %g: group name */
static int bfs_printf_g(CFILE *cfile, const struct bfs_fmt *fmt, const struct BFTW *ftwbuf) {
	const struct bfs_stat *statbuf = bftw_stat(ftwbuf, ftwbuf->stat_flags);
	if (!statbuf) {
		return -1;
	}

	struct bfs_groups *groups = fmt->ptr;
	const struct group *grp = bfs_getgrgid(groups, statbuf->gid);
	if (!grp) {
		return bfs_printf_G(cfile, fmt, ftwbuf);
	}

	return dyn_fprintf(cfile->file, fmt, grp->gr_name);
}

/** %h: leading directories */
static int bfs_printf_h(CFILE *cfile, const struct bfs_fmt *fmt, const struct BFTW *ftwbuf) {
	char *copy = NULL;
	const char *buf;

	if (ftwbuf->nameoff > 0) {
		size_t len = ftwbuf->nameoff;
		if (len > 1) {
			--len;
		}

		buf = copy = strndup(ftwbuf->path, len);
	} else if (ftwbuf->path[0] == '/') {
		buf = "/";
	} else {
		buf = ".";
	}

	if (!buf) {
		return -1;
	}

	int ret;
	if (should_color(cfile, fmt)) {
		ret = cfprintf(cfile, "${di}%pQ${rs}", buf);
	} else {
		ret = dyn_fprintf(cfile->file, fmt, buf);
	}

	free(copy);
	return ret;
}

/** %H: current root */
static int bfs_printf_H(CFILE *cfile, const struct bfs_fmt *fmt, const struct BFTW *ftwbuf) {
	if (should_color(cfile, fmt)) {
		if (ftwbuf->depth == 0) {
			return cfprintf(cfile, "%pP", ftwbuf);
		} else {
			return cfprintf(cfile, "${di}%pQ${rs}", ftwbuf->root);
		}
	} else {
		return dyn_fprintf(cfile->file, fmt, ftwbuf->root);
	}
}

/** %i: inode */
static int bfs_printf_i(CFILE *cfile, const struct bfs_fmt *fmt, const struct BFTW *ftwbuf) {
	const struct bfs_stat *statbuf = bftw_stat(ftwbuf, ftwbuf->stat_flags);
	if (!statbuf) {
		return -1;
	}

	BFS_PRINTF_BUF(buf, "%ju", (uintmax_t)statbuf->ino);
	return dyn_fprintf(cfile->file, fmt, buf);
}

/** %k: 1K blocks */
static int bfs_printf_k(CFILE *cfile, const struct bfs_fmt *fmt, const struct BFTW *ftwbuf) {
	const struct bfs_stat *statbuf = bftw_stat(ftwbuf, ftwbuf->stat_flags);
	if (!statbuf) {
		return -1;
	}

	uintmax_t blocks = ((uintmax_t)statbuf->blocks * BFS_STAT_BLKSIZE + 1023) / 1024;
	BFS_PRINTF_BUF(buf, "%ju", blocks);
	return dyn_fprintf(cfile->file, fmt, buf);
}

/** %l: link target */
static int bfs_printf_l(CFILE *cfile, const struct bfs_fmt *fmt, const struct BFTW *ftwbuf) {
	char *buf = NULL;
	const char *target = "";

	if (ftwbuf->type == BFS_LNK) {
		if (should_color(cfile, fmt)) {
			return cfprintf(cfile, "%pL", ftwbuf);
		}

		const struct bfs_stat *statbuf = bftw_cached_stat(ftwbuf, BFS_STAT_NOFOLLOW);
		size_t len = statbuf ? statbuf->size : 0;

		target = buf = xreadlinkat(ftwbuf->at_fd, ftwbuf->at_path, len);
		if (!target) {
			return -1;
		}
	}

	int ret = dyn_fprintf(cfile->file, fmt, target);
	free(buf);
	return ret;
}

/** %m: mode */
static int bfs_printf_m(CFILE *cfile, const struct bfs_fmt *fmt, const struct BFTW *ftwbuf) {
	const struct bfs_stat *statbuf = bftw_stat(ftwbuf, ftwbuf->stat_flags);
	if (!statbuf) {
		return -1;
	}

	return dyn_fprintf(cfile->file, fmt, (unsigned int)(statbuf->mode & 07777));
}

/** %M: symbolic mode */
static int bfs_printf_M(CFILE *cfile, const struct bfs_fmt *fmt, const struct BFTW *ftwbuf) {
	const struct bfs_stat *statbuf = bftw_stat(ftwbuf, ftwbuf->stat_flags);
	if (!statbuf) {
		return -1;
	}

	char buf[11];
	xstrmode(statbuf->mode, buf);
	return dyn_fprintf(cfile->file, fmt, buf);
}

/** %n: link count */
static int bfs_printf_n(CFILE *cfile, const struct bfs_fmt *fmt, const struct BFTW *ftwbuf) {
	const struct bfs_stat *statbuf = bftw_stat(ftwbuf, ftwbuf->stat_flags);
	if (!statbuf) {
		return -1;
	}

	BFS_PRINTF_BUF(buf, "%ju", (uintmax_t)statbuf->nlink);
	return dyn_fprintf(cfile->file, fmt, buf);
}

/** %p: full path */
static int bfs_printf_p(CFILE *cfile, const struct bfs_fmt *fmt, const struct BFTW *ftwbuf) {
	if (should_color(cfile, fmt)) {
		return cfprintf(cfile, "%pP", ftwbuf);
	} else {
		return dyn_fprintf(cfile->file, fmt, ftwbuf->path);
	}
}

/** %P: path after root */
static int bfs_printf_P(CFILE *cfile, const struct bfs_fmt *fmt, const struct BFTW *ftwbuf) {
	size_t offset = strlen(ftwbuf->root);
	if (ftwbuf->path[offset] == '/') {
		++offset;
	}

	if (should_color(cfile, fmt)) {
		if (ftwbuf->depth == 0) {
			return 0;
		}

		struct BFTW copybuf = *ftwbuf;
		copybuf.path += offset;
		copybuf.nameoff -= offset;
		return cfprintf(cfile, "%pP", &copybuf);
	} else {
		return dyn_fprintf(cfile->file, fmt, ftwbuf->path + offset);
	}
}

/** %s: size */
static int bfs_printf_s(CFILE *cfile, const struct bfs_fmt *fmt, const struct BFTW *ftwbuf) {
	const struct bfs_stat *statbuf = bftw_stat(ftwbuf, ftwbuf->stat_flags);
	if (!statbuf) {
		return -1;
	}

	BFS_PRINTF_BUF(buf, "%ju", (uintmax_t)statbuf->size);
	return dyn_fprintf(cfile->file, fmt, buf);
}

/** %S: sparseness */
static int bfs_printf_S(CFILE *cfile, const struct bfs_fmt *fmt, const struct BFTW *ftwbuf) {
	const struct bfs_stat *statbuf = bftw_stat(ftwbuf, ftwbuf->stat_flags);
	if (!statbuf) {
		return -1;
	}

	double sparsity;
	if (statbuf->size == 0 && statbuf->blocks == 0) {
		sparsity = 1.0;
	} else {
		sparsity = (double)BFS_STAT_BLKSIZE * statbuf->blocks / statbuf->size;
	}
	return dyn_fprintf(cfile->file, fmt, sparsity);
}

/** %U: uid */
static int bfs_printf_U(CFILE *cfile, const struct bfs_fmt *fmt, const struct BFTW *ftwbuf) {
	const struct bfs_stat *statbuf = bftw_stat(ftwbuf, ftwbuf->stat_flags);
	if (!statbuf) {
		return -1;
	}

	BFS_PRINTF_BUF(buf, "%ju", (uintmax_t)statbuf->uid);
	return dyn_fprintf(cfile->file, fmt, buf);
}

/** %u: user name */
static int bfs_printf_u(CFILE *cfile, const struct bfs_fmt *fmt, const struct BFTW *ftwbuf) {
	const struct bfs_stat *statbuf = bftw_stat(ftwbuf, ftwbuf->stat_flags);
	if (!statbuf) {
		return -1;
	}

	struct bfs_users *users = fmt->ptr;
	const struct passwd *pwd = bfs_getpwuid(users, statbuf->uid);
	if (!pwd) {
		return bfs_printf_U(cfile, fmt, ftwbuf);
	}

	return dyn_fprintf(cfile->file, fmt, pwd->pw_name);
}

static const char *bfs_printf_type(enum bfs_type type) {
	switch (type) {
	case BFS_BLK:
		return "b";
	case BFS_CHR:
		return "c";
	case BFS_DIR:
		return "d";
	case BFS_DOOR:
		return "D";
	case BFS_FIFO:
		return "p";
	case BFS_LNK:
		return "l";
	case BFS_REG:
		return "f";
	case BFS_SOCK:
		return "s";
	default:
		return "U";
	}
}

/** %y: type */
static int bfs_printf_y(CFILE *cfile, const struct bfs_fmt *fmt, const struct BFTW *ftwbuf) {
	const char *type = bfs_printf_type(ftwbuf->type);
	return dyn_fprintf(cfile->file, fmt, type);
}

/** %Y: target type */
static int bfs_printf_Y(CFILE *cfile, const struct bfs_fmt *fmt, const struct BFTW *ftwbuf) {
	int error = 0;

	if (ftwbuf->type != BFS_LNK) {
		return bfs_printf_y(cfile, fmt, ftwbuf);
	}

	const char *type = "U";

	const struct bfs_stat *statbuf = bftw_stat(ftwbuf, BFS_STAT_FOLLOW);
	if (statbuf) {
		type = bfs_printf_type(bfs_mode_to_type(statbuf->mode));
	} else {
		switch (errno) {
		case ELOOP:
			type = "L";
			break;
		case ENOENT:
		case ENOTDIR:
			type = "N";
			break;
		default:
			type = "?";
			error = errno;
			break;
		}
	}

	int ret = dyn_fprintf(cfile->file, fmt, type);
	if (error != 0) {
		ret = -1;
		errno = error;
	}
	return ret;
}

/**
 * Append a literal string to the chain.
 */
static int append_literal(const struct bfs_ctx *ctx, struct bfs_printf *format, dchar **literal) {
	if (dstrlen(*literal) == 0) {
		return 0;
	}

	struct bfs_fmt *fmt = RESERVE(struct bfs_fmt, &format->fmts, &format->nfmts);
	if (!fmt) {
		bfs_perror(ctx, "RESERVE()");
		return -1;
	}

	fmt->fn = bfs_printf_literal;
	fmt->str = *literal;

	*literal = dstralloc(0);
	if (!*literal) {
		bfs_perror(ctx, "dstralloc()");
		return -1;
	}

	return 0;
}

/**
 * Append a printf directive to the chain.
 */
static int append_directive(const struct bfs_ctx *ctx, struct bfs_printf *format, dchar **literal, struct bfs_fmt *fmt) {
	if (append_literal(ctx, format, literal) != 0) {
		return -1;
	}

	struct bfs_fmt *dest = RESERVE(struct bfs_fmt, &format->fmts, &format->nfmts);
	if (!dest) {
		bfs_perror(ctx, "RESERVE()");
		return -1;
	}

	*dest = *fmt;
	return 0;
}

int bfs_printf_parse(const struct bfs_ctx *ctx, struct bfs_expr *expr, const char *format) {
	expr->printf = ZALLOC(struct bfs_printf);
	if (!expr->printf) {
		bfs_perror(ctx, "zalloc()");
		return -1;
	}

	dchar *literal = dstralloc(0);
	if (!literal) {
		bfs_perror(ctx, "dstralloc()");
		goto error;
	}

	for (const char *i = format; *i; ++i) {
		char c = *i;

		if (c == '\\') {
			c = *++i;

			if (c >= '0' && c < '8') {
				c = 0;
				for (int j = 0; j < 3 && *i >= '0' && *i < '8'; ++i, ++j) {
					c *= 8;
					c += *i - '0';
				}
				--i;
				goto one_char;
			}

			switch (c) {
			case 'a':  c = '\a'; break;
			case 'b':  c = '\b'; break;
			case 'f':  c = '\f'; break;
			case 'n':  c = '\n'; break;
			case 'r':  c = '\r'; break;
			case 't':  c = '\t'; break;
			case 'v':  c = '\v'; break;
			case '\\': c = '\\'; break;

			case 'c':
				{
					struct bfs_fmt fmt = {
						.fn = bfs_printf_flush,
					};
					if (append_directive(ctx, expr->printf, &literal, &fmt) != 0) {
						goto error;
					}
					goto done;
				}

			case '\0':
				bfs_expr_error(ctx, expr);
				bfs_error(ctx, "Incomplete escape sequence '\\'.\n");
				goto error;

			default:
				bfs_expr_error(ctx, expr);
				bfs_error(ctx, "Unrecognized escape sequence '\\%c'.\n", c);
				goto error;
			}
		} else if (c == '%') {
			if (i[1] == '%') {
				c = *++i;
				goto one_char;
			}

			struct bfs_fmt fmt = {
				.str = dstralloc(2),
			};
			if (!fmt.str) {
				goto fmt_error;
			}
			if (dstrapp(&fmt.str, c) != 0) {
				bfs_perror(ctx, "dstrapp()");
				goto fmt_error;
			}

			const char *specifier = "s";

			// Parse any flags
			bool must_be_numeric = false;
			while (true) {
				c = *++i;

				switch (c) {
				case '#':
				case '0':
				case '+':
					must_be_numeric = true;
					fallthru;
				case ' ':
				case '-':
					if (strchr(fmt.str, c)) {
						bfs_expr_error(ctx, expr);
						bfs_error(ctx, "Duplicate flag '%c'.\n", c);
						goto fmt_error;
					}
					if (dstrapp(&fmt.str, c) != 0) {
						bfs_perror(ctx, "dstrapp()");
						goto fmt_error;
					}
					continue;
				}

				break;
			}

			// Parse the field width
			while (c >= '0' && c <= '9') {
				if (dstrapp(&fmt.str, c) != 0) {
					bfs_perror(ctx, "dstrapp()");
					goto fmt_error;
				}
				c = *++i;
			}

			// Parse the precision
			if (c == '.') {
				do {
					if (dstrapp(&fmt.str, c) != 0) {
						bfs_perror(ctx, "dstrapp()");
						goto fmt_error;
					}
					c = *++i;
				} while (c >= '0' && c <= '9');
			}

			switch (c) {
			case 'a':
				fmt.fn = bfs_printf_ctime;
				fmt.stat_field = BFS_STAT_ATIME;
				break;
			case 'b':
				fmt.fn = bfs_printf_b;
				break;
			case 'c':
				fmt.fn = bfs_printf_ctime;
				fmt.stat_field = BFS_STAT_CTIME;
				break;
			case 'd':
				fmt.fn = bfs_printf_d;
				specifier = "jd";
				break;
			case 'D':
				fmt.fn = bfs_printf_D;
				break;
			case 'f':
				fmt.fn = bfs_printf_f;
				break;
			case 'F':
				fmt.fn = bfs_printf_F;
				fmt.ptr = (void *)bfs_ctx_mtab(ctx);
				if (!fmt.ptr) {
					int error = errno;
					bfs_expr_error(ctx, expr);
					bfs_error(ctx, "Couldn't parse the mount table: %s.\n", xstrerror(error));
					goto fmt_error;
				}
				break;
			case 'g':
				fmt.fn = bfs_printf_g;
				fmt.ptr = ctx->groups;
				break;
			case 'G':
				fmt.fn = bfs_printf_G;
				break;
			case 'h':
				fmt.fn = bfs_printf_h;
				break;
			case 'H':
				fmt.fn = bfs_printf_H;
				break;
			case 'i':
				fmt.fn = bfs_printf_i;
				break;
			case 'k':
				fmt.fn = bfs_printf_k;
				break;
			case 'l':
				fmt.fn = bfs_printf_l;
				break;
			case 'm':
				fmt.fn = bfs_printf_m;
				specifier = "o";
				break;
			case 'M':
				fmt.fn = bfs_printf_M;
				break;
			case 'n':
				fmt.fn = bfs_printf_n;
				break;
			case 'p':
				fmt.fn = bfs_printf_p;
				break;
			case 'P':
				fmt.fn = bfs_printf_P;
				break;
			case 's':
				fmt.fn = bfs_printf_s;
				break;
			case 'S':
				fmt.fn = bfs_printf_S;
				specifier = "g";
				break;
			case 't':
				fmt.fn = bfs_printf_ctime;
				fmt.stat_field = BFS_STAT_MTIME;
				break;
			case 'u':
				fmt.fn = bfs_printf_u;
				fmt.ptr = ctx->users;
				break;
			case 'U':
				fmt.fn = bfs_printf_U;
				break;
			case 'w':
				fmt.fn = bfs_printf_ctime;
				fmt.stat_field = BFS_STAT_BTIME;
				break;
			case 'y':
				fmt.fn = bfs_printf_y;
				break;
			case 'Y':
				fmt.fn = bfs_printf_Y;
				break;

			case 'A':
				fmt.stat_field = BFS_STAT_ATIME;
				goto fmt_strftime;
			case 'B':
			case 'W':
				fmt.stat_field = BFS_STAT_BTIME;
				goto fmt_strftime;
			case 'C':
				fmt.stat_field = BFS_STAT_CTIME;
				goto fmt_strftime;
			case 'T':
				fmt.stat_field = BFS_STAT_MTIME;
				goto fmt_strftime;

			fmt_strftime:
				fmt.fn = bfs_printf_strftime;
				c = *++i;
				if (!c) {
					bfs_expr_error(ctx, expr);
					bfs_error(ctx, "Incomplete time specifier '%s%c'.\n", fmt.str, i[-1]);
					goto fmt_error;
				} else if (strchr("%+@aAbBcCdDeFgGhHIjklmMnprRsStTuUVwWxXyYzZ", c)) {
					fmt.c = c;
				} else {
					bfs_expr_error(ctx, expr);
					bfs_error(ctx, "Unrecognized time specifier '%%%c%c'.\n", i[-1], c);
					goto fmt_error;
				}
				break;

			case '\0':
				bfs_expr_error(ctx, expr);
				bfs_error(ctx, "Incomplete format specifier '%s'.\n", fmt.str);
				goto fmt_error;

			default:
				bfs_expr_error(ctx, expr);
				bfs_error(ctx, "Unrecognized format specifier '%%%c'.\n", c);
				goto fmt_error;
			}

			if (must_be_numeric && strcmp(specifier, "s") == 0) {
				bfs_expr_error(ctx, expr);
				bfs_error(ctx, "Invalid flags '%s' for string format '%%%c'.\n", fmt.str + 1, c);
				goto fmt_error;
			}

			if (dstrcat(&fmt.str, specifier) != 0) {
				bfs_perror(ctx, "dstrcat()");
				goto fmt_error;
			}

			if (append_directive(ctx, expr->printf, &literal, &fmt) != 0) {
				goto fmt_error;
			}

			continue;

		fmt_error:
			dstrfree(fmt.str);
			goto error;
		}

	one_char:
		if (dstrapp(&literal, c) != 0) {
			bfs_perror(ctx, "dstrapp()");
			goto error;
		}
	}

done:
	if (append_literal(ctx, expr->printf, &literal) != 0) {
		goto error;
	}
	dstrfree(literal);
	return 0;

error:
	dstrfree(literal);
	bfs_printf_free(expr->printf);
	expr->printf = NULL;
	return -1;
}

int bfs_printf(CFILE *cfile, const struct bfs_printf *format, const struct BFTW *ftwbuf) {
	int ret = 0, error = 0;

	for (size_t i = 0; i < format->nfmts; ++i) {
		const struct bfs_fmt *fmt = &format->fmts[i];
		if (fmt->fn(cfile, fmt, ftwbuf) < 0) {
			ret = -1;
			error = errno;
		}
	}

	errno = error;
	return ret;
}

void bfs_printf_free(struct bfs_printf *format) {
	if (!format) {
		return;
	}

	for (size_t i = 0; i < format->nfmts; ++i) {
		dstrfree(format->fmts[i].str);
	}
	free(format->fmts);
	free(format);
}
