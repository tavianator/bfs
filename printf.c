/****************************************************************************
 * bfs                                                                      *
 * Copyright (C) 2017-2019 Tavian Barnes <tavianator@tavianator.com>        *
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

#include "printf.h"
#include "cmdline.h"
#include "color.h"
#include "diag.h"
#include "dstring.h"
#include "expr.h"
#include "mtab.h"
#include "passwd.h"
#include "stat.h"
#include "time.h"
#include "util.h"
#include <assert.h>
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

typedef int bfs_printf_fn(FILE *file, const struct bfs_printf *directive, const struct BFTW *ftwbuf);

struct bfs_printf {
	/** The printing function to invoke. */
	bfs_printf_fn *fn;
	/** String data associated with this directive. */
	char *str;
	/** The stat field to print. */
	enum bfs_stat_field stat_field;
	/** Character data associated with this directive. */
	char c;
	/** Some data used by the directive. */
	const void *ptr;
	/** The next printf directive in the chain. */
	struct bfs_printf *next;
};

/** Print some text as-is. */
static int bfs_printf_literal(FILE *file, const struct bfs_printf *directive, const struct BFTW *ftwbuf) {
	size_t len = dstrlen(directive->str);
	if (fwrite(directive->str, 1, len, file) == len) {
		return 0;
	} else {
		return -1;
	}
}

/** \c: flush */
static int bfs_printf_flush(FILE *file, const struct bfs_printf *directive, const struct BFTW *ftwbuf) {
	return fflush(file);
}

/**
 * Print a value to a temporary buffer before formatting it.
 */
#define BFS_PRINTF_BUF(buf, format, ...)				\
	char buf[256];							\
	int ret = snprintf(buf, sizeof(buf), format, __VA_ARGS__);	\
	assert(ret >= 0 && ret < sizeof(buf));				\
	(void)ret

/** %a, %c, %t: ctime() */
static int bfs_printf_ctime(FILE *file, const struct bfs_printf *directive, const struct BFTW *ftwbuf) {
	// Not using ctime() itself because GNU find adds nanoseconds
	static const char *days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
	static const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

	const struct bfs_stat *statbuf = bftw_stat(ftwbuf, ftwbuf->stat_flags);
	if (!statbuf) {
		return -1;
	}

	const struct timespec *ts = bfs_stat_time(statbuf, directive->stat_field);
	if (!ts) {
		return -1;
	}

	struct tm tm;
	if (xlocaltime(&ts->tv_sec, &tm) != 0) {
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

	return fprintf(file, directive->str, buf);
}

/** %A, %B/%W, %C, %T: strftime() */
static int bfs_printf_strftime(FILE *file, const struct bfs_printf *directive, const struct BFTW *ftwbuf) {
	const struct bfs_stat *statbuf = bftw_stat(ftwbuf, ftwbuf->stat_flags);
	if (!statbuf) {
		return -1;
	}

	const struct timespec *ts = bfs_stat_time(statbuf, directive->stat_field);
	if (!ts) {
		return -1;
	}

	struct tm tm;
	if (xlocaltime(&ts->tv_sec, &tm) != 0) {
		return -1;
	}

	int ret;
	char buf[256];
	char format[] = "% ";
	switch (directive->c) {
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
		ret = snprintf(buf, sizeof(buf), "%2d", (tm.tm_hour + 11)%12 + 1);
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
		format[1] = directive->c;
		ret = strftime(buf, sizeof(buf), format, &tm);
		break;
	}

	assert(ret >= 0 && ret < sizeof(buf));
	(void)ret;

	return fprintf(file, directive->str, buf);
}

/** %b: blocks */
static int bfs_printf_b(FILE *file, const struct bfs_printf *directive, const struct BFTW *ftwbuf) {
	const struct bfs_stat *statbuf = bftw_stat(ftwbuf, ftwbuf->stat_flags);
	if (!statbuf) {
		return -1;
	}

	uintmax_t blocks = ((uintmax_t)statbuf->blocks*BFS_STAT_BLKSIZE + 511)/512;
	BFS_PRINTF_BUF(buf, "%ju", blocks);
	return fprintf(file, directive->str, buf);
}

/** %d: depth */
static int bfs_printf_d(FILE *file, const struct bfs_printf *directive, const struct BFTW *ftwbuf) {
	return fprintf(file, directive->str, (intmax_t)ftwbuf->depth);
}

/** %D: device */
static int bfs_printf_D(FILE *file, const struct bfs_printf *directive, const struct BFTW *ftwbuf) {
	const struct bfs_stat *statbuf = bftw_stat(ftwbuf, ftwbuf->stat_flags);
	if (!statbuf) {
		return -1;
	}

	BFS_PRINTF_BUF(buf, "%ju", (uintmax_t)statbuf->dev);
	return fprintf(file, directive->str, buf);
}

/** %f: file name */
static int bfs_printf_f(FILE *file, const struct bfs_printf *directive, const struct BFTW *ftwbuf) {
	return fprintf(file, directive->str, ftwbuf->path + ftwbuf->nameoff);
}

/** %F: file system type */
static int bfs_printf_F(FILE *file, const struct bfs_printf *directive, const struct BFTW *ftwbuf) {
	const struct bfs_stat *statbuf = bftw_stat(ftwbuf, ftwbuf->stat_flags);
	if (!statbuf) {
		return -1;
	}

	const char *type = bfs_fstype(directive->ptr, statbuf);
	return fprintf(file, directive->str, type);
}

/** %G: gid */
static int bfs_printf_G(FILE *file, const struct bfs_printf *directive, const struct BFTW *ftwbuf) {
	const struct bfs_stat *statbuf = bftw_stat(ftwbuf, ftwbuf->stat_flags);
	if (!statbuf) {
		return -1;
	}

	BFS_PRINTF_BUF(buf, "%ju", (uintmax_t)statbuf->gid);
	return fprintf(file, directive->str, buf);
}

/** %g: group name */
static int bfs_printf_g(FILE *file, const struct bfs_printf *directive, const struct BFTW *ftwbuf) {
	const struct bfs_stat *statbuf = bftw_stat(ftwbuf, ftwbuf->stat_flags);
	if (!statbuf) {
		return -1;
	}

	const struct bfs_groups *groups = directive->ptr;
	const struct group *grp = groups ? bfs_getgrgid(groups, statbuf->gid) : NULL;
	if (!grp) {
		return bfs_printf_G(file, directive, ftwbuf);
	}

	return fprintf(file, directive->str, grp->gr_name);
}

/** %h: leading directories */
static int bfs_printf_h(FILE *file, const struct bfs_printf *directive, const struct BFTW *ftwbuf) {
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

	int ret = fprintf(file, directive->str, buf);
	free(copy);
	return ret;
}

/** %H: current root */
static int bfs_printf_H(FILE *file, const struct bfs_printf *directive, const struct BFTW *ftwbuf) {
	return fprintf(file, directive->str, ftwbuf->root);
}

/** %i: inode */
static int bfs_printf_i(FILE *file, const struct bfs_printf *directive, const struct BFTW *ftwbuf) {
	const struct bfs_stat *statbuf = bftw_stat(ftwbuf, ftwbuf->stat_flags);
	if (!statbuf) {
		return -1;
	}

	BFS_PRINTF_BUF(buf, "%ju", (uintmax_t)statbuf->ino);
	return fprintf(file, directive->str, buf);
}

/** %k: 1K blocks */
static int bfs_printf_k(FILE *file, const struct bfs_printf *directive, const struct BFTW *ftwbuf) {
	const struct bfs_stat *statbuf = bftw_stat(ftwbuf, ftwbuf->stat_flags);
	if (!statbuf) {
		return -1;
	}

	uintmax_t blocks = ((uintmax_t)statbuf->blocks*BFS_STAT_BLKSIZE + 1023)/1024;
	BFS_PRINTF_BUF(buf, "%ju", blocks);
	return fprintf(file, directive->str, buf);
}

/** %l: link target */
static int bfs_printf_l(FILE *file, const struct bfs_printf *directive, const struct BFTW *ftwbuf) {
	if (ftwbuf->typeflag != BFTW_LNK) {
		return 0;
	}

	char *target = xreadlinkat(ftwbuf->at_fd, ftwbuf->at_path, 0);
	if (!target) {
		return -1;
	}

	int ret = fprintf(file, directive->str, target);
	free(target);
	return ret;
}

/** %m: mode */
static int bfs_printf_m(FILE *file, const struct bfs_printf *directive, const struct BFTW *ftwbuf) {
	const struct bfs_stat *statbuf = bftw_stat(ftwbuf, ftwbuf->stat_flags);
	if (!statbuf) {
		return -1;
	}

	return fprintf(file, directive->str, (unsigned int)(statbuf->mode & 07777));
}

/** %M: symbolic mode */
static int bfs_printf_M(FILE *file, const struct bfs_printf *directive, const struct BFTW *ftwbuf) {
	const struct bfs_stat *statbuf = bftw_stat(ftwbuf, ftwbuf->stat_flags);
	if (!statbuf) {
		return -1;
	}

	char buf[11];
	format_mode(statbuf->mode, buf);
	return fprintf(file, directive->str, buf);
}

/** %n: link count */
static int bfs_printf_n(FILE *file, const struct bfs_printf *directive, const struct BFTW *ftwbuf) {
	const struct bfs_stat *statbuf = bftw_stat(ftwbuf, ftwbuf->stat_flags);
	if (!statbuf) {
		return -1;
	}

	BFS_PRINTF_BUF(buf, "%ju", (uintmax_t)statbuf->nlink);
	return fprintf(file, directive->str, buf);
}

/** %p: full path */
static int bfs_printf_p(FILE *file, const struct bfs_printf *directive, const struct BFTW *ftwbuf) {
	return fprintf(file, directive->str, ftwbuf->path);
}

/** %P: path after root */
static int bfs_printf_P(FILE *file, const struct bfs_printf *directive, const struct BFTW *ftwbuf) {
	const char *path = ftwbuf->path + strlen(ftwbuf->root);
	if (path[0] == '/') {
		++path;
	}
	return fprintf(file, directive->str, path);
}

/** %s: size */
static int bfs_printf_s(FILE *file, const struct bfs_printf *directive, const struct BFTW *ftwbuf) {
	const struct bfs_stat *statbuf = bftw_stat(ftwbuf, ftwbuf->stat_flags);
	if (!statbuf) {
		return -1;
	}

	BFS_PRINTF_BUF(buf, "%ju", (uintmax_t)statbuf->size);
	return fprintf(file, directive->str, buf);
}

/** %S: sparseness */
static int bfs_printf_S(FILE *file, const struct bfs_printf *directive, const struct BFTW *ftwbuf) {
	const struct bfs_stat *statbuf = bftw_stat(ftwbuf, ftwbuf->stat_flags);
	if (!statbuf) {
		return -1;
	}

	double sparsity;
	if (statbuf->size == 0 && statbuf->blocks == 0) {
		sparsity = 1.0;
	} else {
		sparsity = (double)BFS_STAT_BLKSIZE*statbuf->blocks/statbuf->size;
	}
	return fprintf(file, directive->str, sparsity);
}

/** %U: uid */
static int bfs_printf_U(FILE *file, const struct bfs_printf *directive, const struct BFTW *ftwbuf) {
	const struct bfs_stat *statbuf = bftw_stat(ftwbuf, ftwbuf->stat_flags);
	if (!statbuf) {
		return -1;
	}

	BFS_PRINTF_BUF(buf, "%ju", (uintmax_t)statbuf->uid);
	return fprintf(file, directive->str, buf);
}

/** %u: user name */
static int bfs_printf_u(FILE *file, const struct bfs_printf *directive, const struct BFTW *ftwbuf) {
	const struct bfs_stat *statbuf = bftw_stat(ftwbuf, ftwbuf->stat_flags);
	if (!statbuf) {
		return -1;
	}

	const struct bfs_users *users = directive->ptr;
	const struct passwd *pwd = users ? bfs_getpwuid(users, statbuf->uid) : NULL;
	if (!pwd) {
		return bfs_printf_U(file, directive, ftwbuf);
	}

	return fprintf(file, directive->str, pwd->pw_name);
}

static const char *bfs_printf_type(enum bftw_typeflag typeflag) {
	switch (typeflag) {
	case BFTW_BLK:
		return "b";
	case BFTW_CHR:
		return "c";
	case BFTW_DIR:
		return "d";
	case BFTW_DOOR:
		return "D";
	case BFTW_FIFO:
		return "p";
	case BFTW_LNK:
		return "l";
	case BFTW_REG:
		return "f";
	case BFTW_SOCK:
		return "s";
	default:
		return "U";
	}
}

/** %y: type */
static int bfs_printf_y(FILE *file, const struct bfs_printf *directive, const struct BFTW *ftwbuf) {
	const char *type = bfs_printf_type(ftwbuf->typeflag);
	return fprintf(file, directive->str, type);
}

/** %Y: target type */
static int bfs_printf_Y(FILE *file, const struct bfs_printf *directive, const struct BFTW *ftwbuf) {
	int error = 0;

	if (ftwbuf->typeflag != BFTW_LNK) {
		return bfs_printf_y(file, directive, ftwbuf);
	}

	const char *type = "U";

	const struct bfs_stat *statbuf = bftw_stat(ftwbuf, BFS_STAT_FOLLOW);
	if (statbuf) {
		type = bfs_printf_type(bftw_mode_typeflag(statbuf->mode));
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

	int ret = fprintf(file, directive->str, type);
	if (error != 0) {
		ret = -1;
		errno = error;
	}
	return ret;
}

/**
 * Free a printf directive.
 */
static void free_directive(struct bfs_printf *directive) {
	if (directive) {
		dstrfree(directive->str);
		free(directive);
	}
}

/**
 * Create a new printf directive.
 */
static struct bfs_printf *new_directive(bfs_printf_fn *fn) {
	struct bfs_printf *directive = malloc(sizeof(*directive));
	if (!directive) {
		perror("malloc()");
		goto error;
	}

	directive->fn = fn;
	directive->str = dstralloc(2);
	if (!directive->str) {
		perror("dstralloc()");
		goto error;
	}
	directive->stat_field = 0;
	directive->c = 0;
	directive->ptr = NULL;
	directive->next = NULL;
	return directive;

error:
	free_directive(directive);
	return NULL;
}

/**
 * Append a printf directive to the chain.
 */
static struct bfs_printf **append_directive(struct bfs_printf **tail, struct bfs_printf *directive) {
	assert(directive);
	*tail = directive;
	return &directive->next;
}

/**
 * Append a literal string to the chain.
 */
static struct bfs_printf **append_literal(struct bfs_printf **tail, struct bfs_printf **literal) {
	struct bfs_printf *directive = *literal;
	if (directive && dstrlen(directive->str) > 0) {
		*literal = NULL;
		return append_directive(tail, directive);
	} else {
		return tail;
	}
}

struct bfs_printf *parse_bfs_printf(const char *format, struct cmdline *cmdline) {
	struct bfs_printf *head = NULL;
	struct bfs_printf **tail = &head;

	struct bfs_printf *literal = new_directive(bfs_printf_literal);
	if (!literal) {
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
				tail = append_literal(tail, &literal);
				struct bfs_printf *directive = new_directive(bfs_printf_flush);
				if (!directive) {
					goto error;
				}
				tail = append_directive(tail, directive);
				goto done;

			case '\0':
				bfs_error(cmdline, "'%s': Incomplete escape sequence '\\'.\n", format);
				goto error;

			default:
				bfs_error(cmdline, "'%s': Unrecognized escape sequence '\\%c'.\n", format, c);
				goto error;
			}
		} else if (c == '%') {
			if (i[1] == '%') {
				c = *++i;
				goto one_char;
			}

			struct bfs_printf *directive = new_directive(NULL);
			if (!directive) {
				goto directive_error;
			}
			if (dstrapp(&directive->str, c) != 0) {
				perror("dstrapp()");
				goto directive_error;
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
					// Fallthrough
				case ' ':
				case '-':
					if (strchr(directive->str, c)) {
						bfs_error(cmdline, "'%s': Duplicate flag '%c'.\n", format, c);
						goto directive_error;
					}
					if (dstrapp(&directive->str, c) != 0) {
						perror("dstrapp()");
						goto directive_error;
					}
					continue;
				}

				break;
			}

			// Parse the field width
			while (c >= '0' && c <= '9') {
				if (dstrapp(&directive->str, c) != 0) {
					perror("dstrapp()");
					goto directive_error;
				}
				c = *++i;
			}

			// Parse the precision
			if (c == '.') {
				do {
					if (dstrapp(&directive->str, c) != 0) {
						perror("dstrapp()");
						goto directive_error;
					}
					c = *++i;
				} while (c >= '0' && c <= '9');
			}

			switch (c) {
			case 'a':
				directive->fn = bfs_printf_ctime;
				directive->stat_field = BFS_STAT_ATIME;
				break;
			case 'b':
				directive->fn = bfs_printf_b;
				break;
			case 'c':
				directive->fn = bfs_printf_ctime;
				directive->stat_field = BFS_STAT_CTIME;
				break;
			case 'd':
				directive->fn = bfs_printf_d;
				specifier = "jd";
				break;
			case 'D':
				directive->fn = bfs_printf_D;
				break;
			case 'f':
				directive->fn = bfs_printf_f;
				break;
			case 'F':
				if (!cmdline->mtab) {
					bfs_error(cmdline, "Couldn't parse the mount table: %s.\n", strerror(cmdline->mtab_error));
					goto directive_error;
				}
				directive->ptr = cmdline->mtab;
				directive->fn = bfs_printf_F;
				break;
			case 'g':
				if (!cmdline->groups) {
					bfs_error(cmdline, "Couldn't parse the group table: %s.\n", strerror(cmdline->groups_error));
					goto directive_error;
				}
				directive->ptr = cmdline->groups;
				directive->fn = bfs_printf_g;
				break;
			case 'G':
				directive->fn = bfs_printf_G;
				break;
			case 'h':
				directive->fn = bfs_printf_h;
				break;
			case 'H':
				directive->fn = bfs_printf_H;
				break;
			case 'i':
				directive->fn = bfs_printf_i;
				break;
			case 'k':
				directive->fn = bfs_printf_k;
				break;
			case 'l':
				directive->fn = bfs_printf_l;
				break;
			case 'm':
				directive->fn = bfs_printf_m;
				specifier = "o";
				break;
			case 'M':
				directive->fn = bfs_printf_M;
				break;
			case 'n':
				directive->fn = bfs_printf_n;
				break;
			case 'p':
				directive->fn = bfs_printf_p;
				break;
			case 'P':
				directive->fn = bfs_printf_P;
				break;
			case 's':
				directive->fn = bfs_printf_s;
				break;
			case 'S':
				directive->fn = bfs_printf_S;
				specifier = "g";
				break;
			case 't':
				directive->fn = bfs_printf_ctime;
				directive->stat_field = BFS_STAT_MTIME;
				break;
			case 'u':
				if (!cmdline->users) {
					bfs_error(cmdline, "Couldn't parse the user table: %s.\n", strerror(cmdline->users_error));
					goto directive_error;
				}
				directive->ptr = cmdline->users;
				directive->fn = bfs_printf_u;
				break;
			case 'U':
				directive->fn = bfs_printf_U;
				break;
			case 'w':
				directive->fn = bfs_printf_ctime;
				directive->stat_field = BFS_STAT_BTIME;
				break;
			case 'y':
				directive->fn = bfs_printf_y;
				break;
			case 'Y':
				directive->fn = bfs_printf_Y;
				break;

			case 'A':
				directive->stat_field = BFS_STAT_ATIME;
				goto directive_strftime;
			case 'B':
			case 'W':
				directive->stat_field = BFS_STAT_BTIME;
				goto directive_strftime;
			case 'C':
				directive->stat_field = BFS_STAT_CTIME;
				goto directive_strftime;
			case 'T':
				directive->stat_field = BFS_STAT_MTIME;
				goto directive_strftime;

			directive_strftime:
				directive->fn = bfs_printf_strftime;
				c = *++i;
				if (!c) {
					bfs_error(cmdline, "'%s': Incomplete time specifier '%s%c'.\n", format, directive->str, i[-1]);
					goto directive_error;
				} else if (strchr("%+@aAbBcCdDeFgGhHIjklmMnprRsStTuUVwWxXyYzZ", c)) {
					directive->c = c;
				} else {
					bfs_error(cmdline, "'%s': Unrecognized time specifier '%%%c%c'.\n", format, i[-1], c);
					goto directive_error;
				}
				break;

			case '\0':
				bfs_error(cmdline, "'%s': Incomplete format specifier '%s'.\n", format, directive->str);
				goto directive_error;

			default:
				bfs_error(cmdline, "'%s': Unrecognized format specifier '%%%c'.\n", format, c);
				goto directive_error;
			}

			if (must_be_numeric && strcmp(specifier, "s") == 0) {
				bfs_error(cmdline, "'%s': Invalid flags '%s' for string format '%%%c'.\n", format, directive->str + 1, c);
				goto directive_error;
			}

			if (dstrcat(&directive->str, specifier) != 0) {
				perror("dstrcat()");
				goto directive_error;
			}

			tail = append_literal(tail, &literal);
			tail = append_directive(tail, directive);

			if (!literal) {
				literal = new_directive(bfs_printf_literal);
				if (!literal) {
					goto error;
				}
			}

			continue;

		directive_error:
			free_directive(directive);
			goto error;
		}

	one_char:
		if (dstrapp(&literal->str, c) != 0) {
			perror("dstrapp()");
			goto error;
		}
	}

done:
	tail = append_literal(tail, &literal);
	if (head) {
		free_directive(literal);
		return head;
	} else {
		return literal;
	}

error:
	free_directive(literal);
	free_bfs_printf(head);
	return NULL;
}

int bfs_printf(FILE *file, const struct bfs_printf *command, const struct BFTW *ftwbuf) {
	int ret = 0, error = 0;

	for (const struct bfs_printf *directive = command; directive; directive = directive->next) {
		if (directive->fn(file, directive, ftwbuf) < 0) {
			ret = -1;
			error = errno;
		}
	}

	errno = error;
	return ret;
}

void free_bfs_printf(struct bfs_printf *command) {
	while (command) {
		struct bfs_printf *next = command->next;
		free_directive(command);
		command = next;
	}
}
