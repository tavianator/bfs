/****************************************************************************
 * bfs                                                                      *
 * Copyright (C) 2017 Tavian Barnes <tavianator@tavianator.com>             *
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
#include "dstring.h"
#include "expr.h"
#include "mtab.h"
#include "stat.h"
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

typedef int bfs_printf_fn(FILE *file, const struct bfs_printf_directive *directive, const struct BFTW *ftwbuf);

/**
 * A single directive in a printf command.
 */
struct bfs_printf_directive {
	/** The printing function to invoke. */
	bfs_printf_fn *fn;
	/** String data associated with this directive. */
	char *str;
	/** The stat field to print. */
	enum bfs_stat_field stat_field;
	/** Character data associated with this directive. */
	char c;
	/** The current mount table. */
	const struct bfs_mtab *mtab;
	/** The next printf directive in the chain. */
	struct bfs_printf_directive *next;
};

/** Print some text as-is. */
static int bfs_printf_literal(FILE *file, const struct bfs_printf_directive *directive, const struct BFTW *ftwbuf) {
	size_t len = dstrlen(directive->str);
	if (fwrite(directive->str, 1, len, file) == len) {
		return 0;
	} else {
		return -1;
	}
}

/** \c: flush */
static int bfs_printf_flush(FILE *file, const struct bfs_printf_directive *directive, const struct BFTW *ftwbuf) {
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

/**
 * Get a particular time field from a struct bfs_stat.
 */
static const struct timespec *get_time_field(const struct bfs_stat *statbuf, enum bfs_stat_field stat_field) {
	if (!(statbuf->mask & stat_field)) {
		errno = ENOTSUP;
		return NULL;
	}

	switch (stat_field) {
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

/** %a, %c, %t: ctime() */
static int bfs_printf_ctime(FILE *file, const struct bfs_printf_directive *directive, const struct BFTW *ftwbuf) {
	// Not using ctime() itself because GNU find adds nanoseconds
	static const char *days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
	static const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

	const struct timespec *ts = get_time_field(ftwbuf->statbuf, directive->stat_field);
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
static int bfs_printf_strftime(FILE *file, const struct bfs_printf_directive *directive, const struct BFTW *ftwbuf) {
	const struct timespec *ts = get_time_field(ftwbuf->statbuf, directive->stat_field);
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
static int bfs_printf_b(FILE *file, const struct bfs_printf_directive *directive, const struct BFTW *ftwbuf) {
	uintmax_t blocks = ((uintmax_t)ftwbuf->statbuf->blocks*BFS_STAT_BLKSIZE + 511)/512;
	BFS_PRINTF_BUF(buf, "%ju", blocks);
	return fprintf(file, directive->str, buf);
}

/** %d: depth */
static int bfs_printf_d(FILE *file, const struct bfs_printf_directive *directive, const struct BFTW *ftwbuf) {
	return fprintf(file, directive->str, (intmax_t)ftwbuf->depth);
}

/** %D: device */
static int bfs_printf_D(FILE *file, const struct bfs_printf_directive *directive, const struct BFTW *ftwbuf) {
	BFS_PRINTF_BUF(buf, "%ju", (uintmax_t)ftwbuf->statbuf->dev);
	return fprintf(file, directive->str, buf);
}

/** %f: file name */
static int bfs_printf_f(FILE *file, const struct bfs_printf_directive *directive, const struct BFTW *ftwbuf) {
	return fprintf(file, directive->str, ftwbuf->path + ftwbuf->nameoff);
}

/** %F: file system type */
static int bfs_printf_F(FILE *file, const struct bfs_printf_directive *directive, const struct BFTW *ftwbuf) {
	const char *type = bfs_fstype(directive->mtab, ftwbuf->statbuf);
	return fprintf(file, directive->str, type);
}

/** %G: gid */
static int bfs_printf_G(FILE *file, const struct bfs_printf_directive *directive, const struct BFTW *ftwbuf) {
	BFS_PRINTF_BUF(buf, "%ju", (uintmax_t)ftwbuf->statbuf->gid);
	return fprintf(file, directive->str, buf);
}

/** %g: group name */
static int bfs_printf_g(FILE *file, const struct bfs_printf_directive *directive, const struct BFTW *ftwbuf) {
	struct group *grp = getgrgid(ftwbuf->statbuf->gid);
	if (!grp) {
		return bfs_printf_G(file, directive, ftwbuf);
	}

	return fprintf(file, directive->str, grp->gr_name);
}

/** %h: leading directories */
static int bfs_printf_h(FILE *file, const struct bfs_printf_directive *directive, const struct BFTW *ftwbuf) {
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
static int bfs_printf_H(FILE *file, const struct bfs_printf_directive *directive, const struct BFTW *ftwbuf) {
	return fprintf(file, directive->str, ftwbuf->root);
}

/** %i: inode */
static int bfs_printf_i(FILE *file, const struct bfs_printf_directive *directive, const struct BFTW *ftwbuf) {
	BFS_PRINTF_BUF(buf, "%ju", (uintmax_t)ftwbuf->statbuf->ino);
	return fprintf(file, directive->str, buf);
}

/** %k: 1K blocks */
static int bfs_printf_k(FILE *file, const struct bfs_printf_directive *directive, const struct BFTW *ftwbuf) {
	uintmax_t blocks = ((uintmax_t)ftwbuf->statbuf->blocks*BFS_STAT_BLKSIZE + 1023)/1024;
	BFS_PRINTF_BUF(buf, "%ju", blocks);
	return fprintf(file, directive->str, buf);
}

/** %l: link target */
static int bfs_printf_l(FILE *file, const struct bfs_printf_directive *directive, const struct BFTW *ftwbuf) {
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
static int bfs_printf_m(FILE *file, const struct bfs_printf_directive *directive, const struct BFTW *ftwbuf) {
	return fprintf(file, directive->str, (unsigned int)(ftwbuf->statbuf->mode & 07777));
}

/** %M: symbolic mode */
static int bfs_printf_M(FILE *file, const struct bfs_printf_directive *directive, const struct BFTW *ftwbuf) {
	char buf[11];
	format_mode(ftwbuf->statbuf->mode, buf);
	return fprintf(file, directive->str, buf);
}

/** %n: link count */
static int bfs_printf_n(FILE *file, const struct bfs_printf_directive *directive, const struct BFTW *ftwbuf) {
	BFS_PRINTF_BUF(buf, "%ju", (uintmax_t)ftwbuf->statbuf->nlink);
	return fprintf(file, directive->str, buf);
}

/** %p: full path */
static int bfs_printf_p(FILE *file, const struct bfs_printf_directive *directive, const struct BFTW *ftwbuf) {
	return fprintf(file, directive->str, ftwbuf->path);
}

/** %P: path after root */
static int bfs_printf_P(FILE *file, const struct bfs_printf_directive *directive, const struct BFTW *ftwbuf) {
	const char *path = ftwbuf->path + strlen(ftwbuf->root);
	if (path[0] == '/') {
		++path;
	}
	return fprintf(file, directive->str, path);
}

/** %s: size */
static int bfs_printf_s(FILE *file, const struct bfs_printf_directive *directive, const struct BFTW *ftwbuf) {
	BFS_PRINTF_BUF(buf, "%ju", (uintmax_t)ftwbuf->statbuf->size);
	return fprintf(file, directive->str, buf);
}

/** %S: sparseness */
static int bfs_printf_S(FILE *file, const struct bfs_printf_directive *directive, const struct BFTW *ftwbuf) {
	double sparsity;
	const struct bfs_stat *sb = ftwbuf->statbuf;
	if (sb->size == 0 && sb->blocks == 0) {
		sparsity = 1.0;
	} else {
		sparsity = (double)BFS_STAT_BLKSIZE*sb->blocks/sb->size;
	}
	return fprintf(file, directive->str, sparsity);
}

/** %U: uid */
static int bfs_printf_U(FILE *file, const struct bfs_printf_directive *directive, const struct BFTW *ftwbuf) {
	BFS_PRINTF_BUF(buf, "%ju", (uintmax_t)ftwbuf->statbuf->uid);
	return fprintf(file, directive->str, buf);
}

/** %u: user name */
static int bfs_printf_u(FILE *file, const struct bfs_printf_directive *directive, const struct BFTW *ftwbuf) {
	struct passwd *pwd = getpwuid(ftwbuf->statbuf->uid);
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
static int bfs_printf_y(FILE *file, const struct bfs_printf_directive *directive, const struct BFTW *ftwbuf) {
	const char *type = bfs_printf_type(ftwbuf->typeflag);
	return fprintf(file, directive->str, type);
}

/** %Y: target type */
static int bfs_printf_Y(FILE *file, const struct bfs_printf_directive *directive, const struct BFTW *ftwbuf) {
	int error = 0;

	if (ftwbuf->typeflag != BFTW_LNK) {
		return bfs_printf_y(file, directive, ftwbuf);
	}

	const char *type = "U";

	struct bfs_stat sb;
	if (bfs_stat(ftwbuf->at_fd, ftwbuf->at_path, 0, 0, &sb) == 0) {
		type = bfs_printf_type(mode_to_typeflag(sb.mode));
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
static void free_directive(struct bfs_printf_directive *directive) {
	if (directive) {
		dstrfree(directive->str);
		free(directive);
	}
}

/**
 * Create a new printf directive.
 */
static struct bfs_printf_directive *new_directive() {
	struct bfs_printf_directive *directive = malloc(sizeof(*directive));
	if (!directive) {
		perror("malloc()");
		goto error;
	}

	directive->fn = NULL;
	directive->str = dstralloc(2);
	if (!directive->str) {
		perror("dstralloc()");
		goto error;
	}
	directive->stat_field = 0;
	directive->c = 0;
	directive->mtab = NULL;
	directive->next = NULL;
	return directive;

error:
	free_directive(directive);
	return NULL;
}

/**
 * Append a printf directive to the chain.
 */
static void append_directive(struct bfs_printf_directive ***tail, struct bfs_printf_directive *directive) {
	**tail = directive;
	*tail = &directive->next;
}

/**
 * Append a literal string to the chain.
 */
static int append_literal(struct bfs_printf_directive ***tail, struct bfs_printf_directive **literal, bool last) {
	struct bfs_printf_directive *directive = *literal;
	if (!directive || dstrlen(directive->str) == 0) {
		return 0;
	}

	directive->fn = bfs_printf_literal;
	append_directive(tail, directive);

	if (last) {
		*literal = NULL;
	} else {
		*literal = new_directive();
		if (!*literal) {
			return -1;
		}
	}

	return 0;
}

struct bfs_printf *parse_bfs_printf(const char *format, struct cmdline *cmdline) {
	CFILE *cerr = cmdline->cerr;

	struct bfs_printf *command = malloc(sizeof(*command));
	if (!command) {
		perror("malloc()");
		return NULL;
	}

	command->directives = NULL;
	command->needs_stat = false;
	struct bfs_printf_directive **tail = &command->directives;

	struct bfs_printf_directive *literal = new_directive();
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
				if (append_literal(&tail, &literal, true) != 0) {
					goto error;
				}
				struct bfs_printf_directive *directive = new_directive();
				if (!directive) {
					goto error;
				}
				directive->fn = bfs_printf_flush;
				append_directive(&tail, directive);
				goto done;

			case '\0':
				cfprintf(cerr, "%{er}error: '%s': Incomplete escape sequence '\\'.%{rs}\n", format);
				goto error;

			default:
				cfprintf(cerr, "%{er}error: '%s': Unrecognized escape sequence '\\%c'.%{rs}\n", format, c);
				goto error;
			}
		} else if (c == '%') {
			if (i[1] == '%') {
				c = *++i;
				goto one_char;
			}

			struct bfs_printf_directive *directive = new_directive();
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
				case ' ':
				case '-':
					if (strchr(directive->str, c)) {
						cfprintf(cerr, "%{er}error: '%s': Duplicate flag '%c'.%{rs}\n", format, c);
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
				command->needs_stat = true;
				break;
			case 'b':
				directive->fn = bfs_printf_b;
				command->needs_stat = true;
				break;
			case 'c':
				directive->fn = bfs_printf_ctime;
				directive->stat_field = BFS_STAT_CTIME;
				command->needs_stat = true;
				break;
			case 'd':
				directive->fn = bfs_printf_d;
				specifier = "jd";
				break;
			case 'D':
				directive->fn = bfs_printf_D;
				command->needs_stat = true;
				break;
			case 'f':
				directive->fn = bfs_printf_f;
				break;
			case 'F':
				if (!cmdline->mtab) {
					cmdline->mtab = parse_bfs_mtab();
					if (!cmdline->mtab) {
						cfprintf(cmdline->cerr, "%{er}error: Couldn't parse the mount table: %m%{rs}\n");
						goto directive_error;
					}
				}
				directive->fn = bfs_printf_F;
				directive->mtab = cmdline->mtab;
				command->needs_stat = true;
				break;
			case 'g':
				directive->fn = bfs_printf_g;
				command->needs_stat = true;
				break;
			case 'G':
				directive->fn = bfs_printf_G;
				command->needs_stat = true;
				break;
			case 'h':
				directive->fn = bfs_printf_h;
				break;
			case 'H':
				directive->fn = bfs_printf_H;
				break;
			case 'i':
				directive->fn = bfs_printf_i;
				command->needs_stat = true;
				break;
			case 'k':
				directive->fn = bfs_printf_k;
				command->needs_stat = true;
				break;
			case 'l':
				directive->fn = bfs_printf_l;
				break;
			case 'm':
				directive->fn = bfs_printf_m;
				specifier = "o";
				command->needs_stat = true;
				break;
			case 'M':
				directive->fn = bfs_printf_M;
				command->needs_stat = true;
				break;
			case 'n':
				directive->fn = bfs_printf_n;
				command->needs_stat = true;
				break;
			case 'p':
				directive->fn = bfs_printf_p;
				break;
			case 'P':
				directive->fn = bfs_printf_P;
				break;
			case 's':
				directive->fn = bfs_printf_s;
				command->needs_stat = true;
				break;
			case 'S':
				directive->fn = bfs_printf_S;
				specifier = "g";
				command->needs_stat = true;
				break;
			case 't':
				directive->fn = bfs_printf_ctime;
				directive->stat_field = BFS_STAT_MTIME;
				command->needs_stat = true;
				break;
			case 'u':
				directive->fn = bfs_printf_u;
				command->needs_stat = true;
				break;
			case 'U':
				directive->fn = bfs_printf_U;
				command->needs_stat = true;
				break;
			case 'w':
				directive->fn = bfs_printf_ctime;
				directive->stat_field = BFS_STAT_BTIME;
				command->needs_stat = true;
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
				command->needs_stat = true;
				c = *++i;
				if (!c) {
					cfprintf(cerr, "%{er}error: '%s': Incomplete time specifier '%s%c'.%{rs}\n",
					         format, directive->str, i[-1]);
					goto directive_error;
				} else if (strchr("%+@aAbBcCdDeFgGhHIjklmMnprRsStTuUVwWxXyYzZ", c)) {
					directive->c = c;
				} else {
					cfprintf(cerr, "%{er}error: '%s': Unrecognized time specifier '%%%c%c'.%{rs}\n",
					         format, i[-1], c);
					goto directive_error;
				}
				break;

			case '\0':
				cfprintf(cerr, "%{er}error: '%s': Incomplete format specifier '%s'.%{rs}\n",
				         format, directive->str);
				goto directive_error;

			default:
				cfprintf(cerr, "%{er}error: '%s': Unrecognized format specifier '%%%c'.%{rs}\n",
				         format, c);
				goto directive_error;
			}

			if (must_be_numeric && strcmp(specifier, "s") == 0) {
				cfprintf(cerr, "%{er}error: '%s': Invalid flags '%s' for string format '%%%c'.%{rs}\n",
				         format, directive->str + 1, c);
				goto directive_error;
			}

			if (dstrcat(&directive->str, specifier) != 0) {
				perror("dstrcat()");
				goto directive_error;
			}

			if (append_literal(&tail, &literal, false) != 0) {
				goto directive_error;
			}
			append_directive(&tail, directive);
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
	if (append_literal(&tail, &literal, true) != 0) {
		goto error;
	}

	free_directive(literal);
	return command;

error:
	free_directive(literal);
	free_bfs_printf(command);
	return NULL;
}

int bfs_printf(FILE *file, const struct bfs_printf *command, const struct BFTW *ftwbuf) {
	int ret = 0, error = 0;

	for (struct bfs_printf_directive *directive = command->directives; directive; directive = directive->next) {
		if (directive->fn(file, directive, ftwbuf) < 0) {
			ret = -1;
			error = errno;
		}
	}

	errno = error;
	return ret;
}

void free_bfs_printf(struct bfs_printf *command) {
	if (command) {
		struct bfs_printf_directive *directive = command->directives;
		while (directive) {
			struct bfs_printf_directive *next = directive->next;
			free_directive(directive);
			directive = next;
		}

		free(command);
	}
}
