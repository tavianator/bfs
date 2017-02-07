/*********************************************************************
 * bfs                                                               *
 * Copyright (C) 2017 Tavian Barnes <tavianator@tavianator.com>      *
 *                                                                   *
 * This program is free software. It comes without any warranty, to  *
 * the extent permitted by applicable law. You can redistribute it   *
 * and/or modify it under the terms of the Do What The Fuck You Want *
 * To Public License, Version 2, as published by Sam Hocevar. See    *
 * the COPYING file or http://www.wtfpl.net/ for more details.       *
 *********************************************************************/

#include "printf.h"
#include "color.h"
#include "dstring.h"
#include "util.h"
#include <assert.h>
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
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
	/** The next printf directive in the chain. */
	struct bfs_printf_directive *next;
};

/** Print some text as-is. */
static int bfs_printf_literal(FILE *file, const struct bfs_printf_directive *directive, const struct BFTW *ftwbuf) {
	return fprintf(file, "%s", directive->str);
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
 * Print a ctime()-style string, for %a, %c, and %t.
 */
static int bfs_printf_ctime(FILE *file, const struct bfs_printf_directive *directive, const struct timespec *ts) {
	// Not using ctime() itself because GNU find adds nanoseconds
	static const char *days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
	static const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

	const struct tm *tm = localtime(&ts->tv_sec);
	BFS_PRINTF_BUF(buf, "%s %s %2d %.2d:%.2d:%.2d.%09ld0 %4d",
		       days[tm->tm_wday],
		       months[tm->tm_mon],
		       tm->tm_mday,
		       tm->tm_hour,
		       tm->tm_min,
		       tm->tm_sec,
		       (long)ts->tv_nsec,
		       1900 + tm->tm_year);

	return fprintf(file, directive->str, buf);
}

/** %a: access time */
static int bfs_printf_a(FILE *file, const struct bfs_printf_directive *directive, const struct BFTW *ftwbuf) {
	return bfs_printf_ctime(file, directive, &ftwbuf->statbuf->st_atim);
}

/** %b: blocks */
static int bfs_printf_b(FILE *file, const struct bfs_printf_directive *directive, const struct BFTW *ftwbuf) {
	BFS_PRINTF_BUF(buf, "%ju", (uintmax_t)ftwbuf->statbuf->st_blocks);
	return fprintf(file, directive->str, buf);
}

/** %c: change time */
static int bfs_printf_c(FILE *file, const struct bfs_printf_directive *directive, const struct BFTW *ftwbuf) {
	return bfs_printf_ctime(file, directive, &ftwbuf->statbuf->st_ctim);
}

/** %d: depth */
static int bfs_printf_d(FILE *file, const struct bfs_printf_directive *directive, const struct BFTW *ftwbuf) {
	return fprintf(file, directive->str, (intmax_t)ftwbuf->depth);
}

/** %D: device */
static int bfs_printf_D(FILE *file, const struct bfs_printf_directive *directive, const struct BFTW *ftwbuf) {
	BFS_PRINTF_BUF(buf, "%ju", (uintmax_t)ftwbuf->statbuf->st_dev);
	return fprintf(file, directive->str, buf);
}

/** %f: file name */
static int bfs_printf_f(FILE *file, const struct bfs_printf_directive *directive, const struct BFTW *ftwbuf) {
	return fprintf(file, directive->str, ftwbuf->path + ftwbuf->nameoff);
}

/** %G: gid */
static int bfs_printf_G(FILE *file, const struct bfs_printf_directive *directive, const struct BFTW *ftwbuf) {
	BFS_PRINTF_BUF(buf, "%ju", (uintmax_t)ftwbuf->statbuf->st_gid);
	return fprintf(file, directive->str, buf);
}

/** %g: group name */
static int bfs_printf_g(FILE *file, const struct bfs_printf_directive *directive, const struct BFTW *ftwbuf) {
	struct group *grp = getgrgid(ftwbuf->statbuf->st_gid);
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
	BFS_PRINTF_BUF(buf, "%ju", (uintmax_t)ftwbuf->statbuf->st_ino);
	return fprintf(file, directive->str, buf);
}

/** %k: 1K blocks */
static int bfs_printf_k(FILE *file, const struct bfs_printf_directive *directive, const struct BFTW *ftwbuf) {
	BFS_PRINTF_BUF(buf, "%ju", (uintmax_t)(ftwbuf->statbuf->st_blocks + 1)/2);
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
	return fprintf(file, directive->str, (unsigned int)(ftwbuf->statbuf->st_mode & 07777));
}

/** %M: symbolic mode */
static int bfs_printf_M(FILE *file, const struct bfs_printf_directive *directive, const struct BFTW *ftwbuf) {
	char buf[] = "----------";

	switch (ftwbuf->typeflag) {
	case BFTW_BLK:
		buf[0] = 'b';
		break;
	case BFTW_CHR:
		buf[0] = 'c';
		break;
	case BFTW_DIR:
		buf[0] = 'd';
		break;
	case BFTW_DOOR:
		buf[0] = 'D';
		break;
	case BFTW_FIFO:
		buf[0] = 'p';
		break;
	case BFTW_LNK:
		buf[0] = 'l';
		break;
	case BFTW_SOCK:
		buf[0] = 's';
		break;
	default:
		break;
	}

	mode_t mode = ftwbuf->statbuf->st_mode;

	if (mode & 00400) {
		buf[1] = 'r';
	}
	if (mode & 00200) {
		buf[2] = 'w';
	}
	if ((mode & 04100) == 04000) {
		buf[3] = 'S';
	} else if (mode & 04000) {
		buf[3] = 's';
	} else if (mode & 00100) {
		buf[3] = 'x';
	}

	if (mode & 00040) {
		buf[4] = 'r';
	}
	if (mode & 00020) {
		buf[5] = 'w';
	}
	if ((mode & 02010) == 02000) {
		buf[6] = 'S';
	} else if (mode & 02000) {
		buf[6] = 's';
	} else if (mode & 00010) {
		buf[6] = 'x';
	}

	if (mode & 00004) {
		buf[7] = 'r';
	}
	if (mode & 00002) {
		buf[8] = 'w';
	}
	if ((mode & 01001) == 01000) {
		buf[9] = 'T';
	} else if (mode & 01000) {
		buf[9] = 't';
	} else if (mode & 00001) {
		buf[9] = 'x';
	}

	return fprintf(file, directive->str, buf);
}

/** %n: link count */
static int bfs_printf_n(FILE *file, const struct bfs_printf_directive *directive, const struct BFTW *ftwbuf) {
	BFS_PRINTF_BUF(buf, "%ju", (uintmax_t)ftwbuf->statbuf->st_nlink);
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
	BFS_PRINTF_BUF(buf, "%ju", (uintmax_t)ftwbuf->statbuf->st_size);
	return fprintf(file, directive->str, buf);
}

/** %S: sparseness */
static int bfs_printf_S(FILE *file, const struct bfs_printf_directive *directive, const struct BFTW *ftwbuf) {
	double sparsity = 512.0 * ftwbuf->statbuf->st_blocks / ftwbuf->statbuf->st_size;
	return fprintf(file, directive->str, sparsity);
}

/** %t: modification time */
static int bfs_printf_t(FILE *file, const struct bfs_printf_directive *directive, const struct BFTW *ftwbuf) {
	return bfs_printf_ctime(file, directive, &ftwbuf->statbuf->st_mtim);
}

/** %U: uid */
static int bfs_printf_U(FILE *file, const struct bfs_printf_directive *directive, const struct BFTW *ftwbuf) {
	BFS_PRINTF_BUF(buf, "%ju", (uintmax_t)ftwbuf->statbuf->st_uid);
	return fprintf(file, directive->str, buf);
}

/** %u: user name */
static int bfs_printf_u(FILE *file, const struct bfs_printf_directive *directive, const struct BFTW *ftwbuf) {
	struct passwd *pwd = getpwuid(ftwbuf->statbuf->st_uid);
	if (!pwd) {
		return bfs_printf_U(file, directive, ftwbuf);
	}

	return fprintf(file, directive->str, pwd->pw_name);
}

/** %y: type */
static int bfs_printf_y(FILE *file, const struct bfs_printf_directive *directive, const struct BFTW *ftwbuf) {
	const char *type;
	switch (ftwbuf->typeflag) {
	case BFTW_BLK:
		type = "b";
		break;
	case BFTW_CHR:
		type = "c";
		break;
	case BFTW_DIR:
		type = "d";
		break;
	case BFTW_DOOR:
		type = "D";
		break;
	case BFTW_FIFO:
		type = "p";
		break;
	case BFTW_LNK:
		type = "l";
		break;
	case BFTW_REG:
		type = "f";
		break;
	case BFTW_SOCK:
		type = "s";
		break;
	default:
		type = "U";
		break;
	}

	return fprintf(file, directive->str, type);
}

/** %Y: target type */
static int bfs_printf_Y(FILE *file, const struct bfs_printf_directive *directive, const struct BFTW *ftwbuf) {
	if (ftwbuf->typeflag != BFTW_LNK) {
		return bfs_printf_y(file, directive, ftwbuf);
	}

	const char *type = "U";

	struct stat sb;
	if (fstatat(ftwbuf->at_fd, ftwbuf->at_path, &sb, 0) == 0) {
		switch (sb.st_mode & S_IFMT) {
#ifdef S_IFBLK
		case S_IFBLK:
			type = "b";
			break;
#endif
#ifdef S_IFCHR
		case S_IFCHR:
			type = "c";
			break;
#endif
#ifdef S_IFDIR
		case S_IFDIR:
			type = "d";
			break;
#endif
#ifdef S_IFDOOR
		case S_IFDOOR:
			type = "D";
			break;
#endif
#ifdef S_IFIFO
		case S_IFIFO:
			type = "p";
			break;
#endif
#ifdef S_IFLNK
		case S_IFLNK:
			type = "l";
			break;
#endif
#ifdef S_IFREG
		case S_IFREG:
			type = "f";
			break;
#endif
#ifdef S_IFSOCK
		case S_IFSOCK:
			type = "s";
			break;
#endif
		}
	} else {
		switch (errno) {
		case ELOOP:
			type = "L";
			break;
		case ENOENT:
			type = "N";
			break;
		}
	}

	return fprintf(file, directive->str, type);
}

/**
 * Append a printf directive to the chain.
 */
static int append_directive(struct bfs_printf_directive ***tail, bfs_printf_fn *fn, char *str) {
	struct bfs_printf_directive *directive = malloc(sizeof(*directive));
	if (!directive) {
		perror("malloc()");
		return -1;
	}

	directive->fn = fn;
	directive->str = str;
	directive->next = NULL;
	**tail = directive;
	*tail = &directive->next;
	return 0;
}

/**
 * Append a literal string to the chain.
 */
static int append_literal(struct bfs_printf_directive ***tail, char **literal, bool last) {
	if (!*literal || dstrlen(*literal) == 0) {
		return 0;
	}

	if (append_directive(tail, bfs_printf_literal, *literal) != 0) {
		return -1;
	}

	if (last) {
		*literal = NULL;
	} else {
		*literal = dstralloc(0);
		if (!*literal) {
			perror("dstralloc()");
			return -1;
		}
	}

	return 0;
}

struct bfs_printf *parse_bfs_printf(const char *format, const struct colors *stderr_colors) {
	struct bfs_printf *command = malloc(sizeof(*command));
	if (!command) {
		return NULL;
	}

	command->directives = NULL;
	command->needs_stat = false;
	struct bfs_printf_directive **tail = &command->directives;

	char *literal = dstralloc(0);

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
				if (append_directive(&tail, bfs_printf_flush, NULL) != 0) {
					goto error;
				}
				goto done;

			case '\0':
				pretty_error(stderr_colors,
				             "error: '%s': Incomplete escape sequence '\\'.\n",
				             format);
				goto error;

			default:
				pretty_error(stderr_colors,
				             "error: '%s': Unrecognized escape sequence '\\%c'.\n",
				             format, c);
				goto error;
			}
		} else if (c == '%') {
			bfs_printf_fn *fn;

			char *directive = dstralloc(2);
			if (!directive) {
				perror("dstralloc()");
				goto directive_error;
			}
			dstrncat(&directive, &c, 1);

			const char *specifier = "s";

			// Parse any flags
			bool must_be_int = false;
			while (true) {
				c = *++i;

				switch (c) {
				case '#':
				case '0':
				case '+':
					must_be_int = true;
				case ' ':
				case '-':
					if (strchr(directive, c)) {
						pretty_error(stderr_colors,
						             "error: '%s': Duplicate flag '%c'.\n",
						             format, c);
						goto directive_error;
					}
					if (dstrncat(&directive, &c, 1) != 0) {
						perror("dstrncat()");
						goto directive_error;
					}
					continue;
				}

				break;
			}

			// Parse the field width
			while (c >= '0' && c <= '9') {
				if (dstrncat(&directive, &c, 1) != 0) {
					perror("dstrncat()");
					goto directive_error;
				}
				c = *++i;
			}

			// Parse the precision
			if (c == '.') {
				do {
					if (dstrncat(&directive, &c, 1) != 0) {
						perror("dstrncat()");
						goto directive_error;
					}
					c = *++i;
				} while (c >= '0' && c <= '9');
			}

			switch (c) {
			case '%':
				dstrfree(directive);
				goto one_char;
			case 'a':
				fn = bfs_printf_a;
				command->needs_stat = true;
				break;
			case 'b':
				fn = bfs_printf_b;
				command->needs_stat = true;
				break;
			case 'c':
				fn = bfs_printf_c;
				command->needs_stat = true;
				break;
			case 'd':
				fn = bfs_printf_d;
				specifier = "jd";
				break;
			case 'D':
				fn = bfs_printf_D;
				command->needs_stat = true;
				break;
			case 'f':
				fn = bfs_printf_f;
				break;
			case 'g':
				fn = bfs_printf_g;
				command->needs_stat = true;
				break;
			case 'G':
				fn = bfs_printf_G;
				command->needs_stat = true;
				break;
			case 'h':
				fn = bfs_printf_h;
				break;
			case 'H':
				fn = bfs_printf_H;
				break;
			case 'i':
				fn = bfs_printf_i;
				command->needs_stat = true;
				break;
			case 'k':
				fn = bfs_printf_k;
				command->needs_stat = true;
				break;
			case 'l':
				fn = bfs_printf_l;
				break;
			case 'm':
				fn = bfs_printf_m;
				specifier = "o";
				command->needs_stat = true;
				break;
			case 'M':
				fn = bfs_printf_M;
				command->needs_stat = true;
				break;
			case 'n':
				fn = bfs_printf_n;
				command->needs_stat = true;
				break;
			case 'p':
				fn = bfs_printf_p;
				break;
			case 'P':
				fn = bfs_printf_P;
				break;
			case 's':
				fn = bfs_printf_s;
				command->needs_stat = true;
				break;
			case 'S':
				fn = bfs_printf_S;
				specifier = "g";
				command->needs_stat = true;
				break;
			case 't':
				fn = bfs_printf_t;
				command->needs_stat = true;
				break;
			case 'u':
				fn = bfs_printf_u;
				command->needs_stat = true;
				break;
			case 'U':
				fn = bfs_printf_U;
				command->needs_stat = true;
				break;
			case 'y':
				fn = bfs_printf_y;
				break;
			case 'Y':
				fn = bfs_printf_Y;
				break;

			case '\0':
				pretty_error(stderr_colors,
				             "error: '%s': Incomplete format specifier '%s'.\n",
				             format, directive);
				goto directive_error;

			default:
				pretty_error(stderr_colors,
				             "error: '%s': Unrecognized format specifier '%%%c'.\n",
				             format, c);
				goto directive_error;
			}

			if (must_be_int && strcmp(specifier, "s") == 0) {
				pretty_error(stderr_colors,
				             "error: '%s': Invalid flags '%s' for string format '%%%c'.\n",
				             format, directive + 1, c);
				goto directive_error;
			}

			if (dstrcat(&directive, specifier) != 0) {
				perror("dstrcat()");
				goto directive_error;
			}

			if (append_literal(&tail, &literal, false) != 0) {
				goto directive_error;
			}
			if (append_directive(&tail, fn, directive) != 0) {
				goto directive_error;
			}
			continue;

		directive_error:
			dstrfree(directive);
			goto error;
		}

	one_char:
		if (dstrncat(&literal, &c, 1) != 0) {
			perror("dstrncat()");
			goto error;
		}
	}

done:
	if (append_literal(&tail, &literal, true) != 0) {
		goto error;
	}

	dstrfree(literal);
	return command;

error:
	dstrfree(literal);
	free_bfs_printf(command);
	return NULL;
}

int bfs_printf(FILE *file, const struct bfs_printf *command, const struct BFTW *ftwbuf) {
	int ret = -1;

	for (struct bfs_printf_directive *directive = command->directives; directive; directive = directive->next) {
		if (directive->fn(file, directive, ftwbuf) < 0) {
			goto done;
		}
	}

	ret = 0;
done:
	return ret;
}

void free_bfs_printf(struct bfs_printf *command) {
	if (command) {
		struct bfs_printf_directive *directive = command->directives;
		while (directive) {
			struct bfs_printf_directive *next = directive->next;
			dstrfree(directive->str);
			free(directive);
			directive = next;
		}

		free(command);
	}
}
