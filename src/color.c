// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include "color.h"
#include "alloc.h"
#include "bfstd.h"
#include "bftw.h"
#include "config.h"
#include "diag.h"
#include "dir.h"
#include "dstring.h"
#include "expr.h"
#include "fsade.h"
#include "stat.h"
#include "trie.h"
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/**
 * An escape sequence, which may contain embedded NUL bytes.
 */
struct esc_seq {
	/** The length of the escape sequence. */
	size_t len;
	/** The escape sequence iteself, without a terminating NUL. */
	char seq[];
};

/**
 * A colored file extension, like `*.tar=01;31`.
 */
struct ext_color {
	/** Priority, to disambiguate case-sensitive and insensitive matches. */
	size_t priority;
	/** The escape sequence associated with this extension. */
	struct esc_seq *esc;
	/** The length of the extension to match. */
	size_t len;
	/** Whether the comparison should be case-sensitive. */
	bool case_sensitive;
	/** The extension to match (NUL-terminated). */
	char ext[];
};

struct colors {
	/** esc_seq allocator. */
	struct varena esc_arena;
	/** ext_color allocator. */
	struct varena ext_arena;

	// Known dircolors keys

	struct esc_seq *reset;
	struct esc_seq *leftcode;
	struct esc_seq *rightcode;
	struct esc_seq *endcode;
	struct esc_seq *clear_to_eol;

	struct esc_seq *bold;
	struct esc_seq *gray;
	struct esc_seq *red;
	struct esc_seq *green;
	struct esc_seq *yellow;
	struct esc_seq *blue;
	struct esc_seq *magenta;
	struct esc_seq *cyan;
	struct esc_seq *white;

	struct esc_seq *warning;
	struct esc_seq *error;

	struct esc_seq *normal;

	struct esc_seq *file;
	struct esc_seq *multi_hard;
	struct esc_seq *executable;
	struct esc_seq *capable;
	struct esc_seq *setgid;
	struct esc_seq *setuid;

	struct esc_seq *directory;
	struct esc_seq *sticky;
	struct esc_seq *other_writable;
	struct esc_seq *sticky_other_writable;

	struct esc_seq *link;
	struct esc_seq *orphan;
	struct esc_seq *missing;
	bool link_as_target;

	struct esc_seq *blockdev;
	struct esc_seq *chardev;
	struct esc_seq *door;
	struct esc_seq *pipe;
	struct esc_seq *socket;

	/** A mapping from color names (fi, di, ln, etc.) to struct fields. */
	struct trie names;

	/** Number of extensions. */
	size_t ext_count;
	/** Longest extension. */
	size_t ext_len;
	/** Case-sensitive extension trie. */
	struct trie ext_trie;
	/** Case-insensitive extension trie. */
	struct trie iext_trie;
};

/** Allocate an escape sequence. */
static struct esc_seq *new_esc(struct colors *colors, const char *seq, size_t len) {
	struct esc_seq *esc = varena_alloc(&colors->esc_arena, len);
	if (esc) {
		esc->len = len;
		memcpy(esc->seq, seq, len);
	}
	return esc;
}

/** Free an escape sequence. */
static void free_esc(struct colors *colors, struct esc_seq *seq) {
	varena_free(&colors->esc_arena, seq, seq->len);
}

/** Initialize a color in the table. */
static int init_esc(struct colors *colors, const char *name, const char *value, struct esc_seq **field) {
	struct esc_seq *esc = NULL;
	if (value) {
		esc = new_esc(colors, value, strlen(value));
		if (!esc) {
			return -1;
		}
	}

	*field = esc;

	struct trie_leaf *leaf = trie_insert_str(&colors->names, name);
	if (leaf) {
		leaf->value = field;
		return 0;
	} else {
		return -1;
	}
}

/** Check if an escape sequence is equal to a string. */
static bool esc_eq(const struct esc_seq *esc, const char *str, size_t len) {
	return esc->len == len && memcmp(esc->seq, str, len) == 0;
}

/** Get an escape sequence from the table. */
static struct esc_seq **get_esc(const struct colors *colors, const char *name) {
	const struct trie_leaf *leaf = trie_find_str(&colors->names, name);
	return leaf ? leaf->value : NULL;
}

/** Set a named escape sequence. */
static int set_esc(struct colors *colors, const char *name, char *value) {
	struct esc_seq **field = get_esc(colors, name);
	if (!field) {
		return 0;
	}

	if (*field) {
		free_esc(colors, *field);
		*field = NULL;
	}

	if (value) {
		*field = new_esc(colors, value, dstrlen(value));
		if (!*field) {
			return -1;
		}
	}

	return 0;
}

/** Reverse a string, to turn suffix matches into prefix matches. */
static void ext_reverse(char *ext, size_t len) {
	for (size_t i = 0, j = len - 1; len && i < j; ++i, --j) {
		char c = ext[i];
		ext[i] = ext[j];
		ext[j] = c;
	}
}

/** Convert a string to lowercase for case-insensitive matching. */
static void ext_tolower(char *ext, size_t len) {
	for (size_t i = 0; i < len; ++i) {
		char c = ext[i];

		// What's internationalization?  Doesn't matter, this is what
		// GNU ls does.  Luckily, since there's no standard C way to
		// casefold.  Not using tolower() here since it respects the
		// current locale, which GNU ls doesn't do.
		if (c >= 'A' && c <= 'Z') {
			c += 'a' - 'A';
		}

		ext[i] = c;
	}
}

/**
 * The "smart case" algorithm.
 *
 * @param ext
 *         The current extension being added.
 * @param prev
 *         The previous case-sensitive match, if any, for the same extension.
 * @param iprev
 *         The previous case-insensitive match, if any, for the same extension.
 * @return
 *         Whether this extension should become case-sensitive.
 */
static bool ext_case_sensitive(struct ext_color *ext, struct ext_color *prev, struct ext_color *iprev) {
	// This is the first case-insensitive occurrence of this extension, e.g.
	//
	//     *.gz=01;31:*.tar.gz=01;33
	if (!iprev) {
		bfs_assert(!prev);
		return false;
	}

	// If the last version of this extension is already case-sensitive,
	// this one should be too, e.g.
	//
	//     *.tar.gz=01;31:*.TAR.GZ=01;32:*.TAR.GZ=01;33
	if (iprev->case_sensitive) {
		return true;
	}

	// The case matches the last occurrence exactly, e.g.
	//
	//     *.tar.gz=01;31:*.tar.gz=01;33
	if (iprev == prev) {
		return false;
	}

	// Different case, but same value, e.g.
	//
	//     *.tar.gz=01;31:*.TAR.GZ=01;31
	if (esc_eq(iprev->esc, ext->esc->seq, ext->esc->len)) {
		return false;
	}

	// Different case, different value, e.g.
	//
	//     *.tar.gz=01;31:*.TAR.GZ=01;33
	return true;
}

/** Set the color for an extension. */
static int set_ext(struct colors *colors, char *key, char *value) {
	size_t len = dstrlen(key);
	struct ext_color *ext = varena_alloc(&colors->ext_arena, len + 1);
	if (!ext) {
		return -1;
	}

	ext->priority = colors->ext_count++;
	ext->len = len;
	ext->case_sensitive = false;
	ext->esc = new_esc(colors, value, dstrlen(value));
	if (!ext->esc) {
		goto fail;
	}

	key = memcpy(ext->ext, key, len + 1);

	// Reverse the extension (`*.y.x` -> `x.y.*`) so we can use trie_find_prefix()
	ext_reverse(key, len);

	// Find any pre-existing exact match
	struct ext_color *prev = NULL;
	struct trie_leaf *leaf = trie_find_str(&colors->ext_trie, key);
	if (leaf) {
		prev = leaf->value;
		trie_remove(&colors->ext_trie, leaf);
	}

	// A later *.x should override any earlier *.x, *.y.x, etc.
	while ((leaf = trie_find_postfix(&colors->ext_trie, key))) {
		trie_remove(&colors->ext_trie, leaf);
	}

	// Insert the extension into the case-sensitive trie
	leaf = trie_insert_str(&colors->ext_trie, key);
	if (!leaf) {
		goto fail;
	}
	leaf->value = ext;

	// "Smart case": if the same extension is given with two different
	// capitalizations (e.g. `*.y.x=31:*.Y.Z=32:`), make it case-sensitive
	ext_tolower(key, len);
	leaf = trie_insert_str(&colors->iext_trie, key);
	if (!leaf) {
		goto fail;
	}

	struct ext_color *iprev = leaf->value;
	if (ext_case_sensitive(ext, prev, iprev)) {
		iprev->case_sensitive = true;
		ext->case_sensitive = true;
	}
	leaf->value = ext;

	return 0;

fail:
	if (ext->esc) {
		free_esc(colors, ext->esc);
	}
	varena_free(&colors->ext_arena, ext, len + 1);
	return -1;
}

/** Rebuild the case-insensitive trie after all extensions have been parsed. */
static int build_iext_trie(struct colors *colors) {
	trie_destroy(&colors->iext_trie);
	trie_init(&colors->iext_trie);

	TRIE_FOR_EACH(&colors->ext_trie, leaf) {
		size_t len = leaf->length - 1;
		if (colors->ext_len < len) {
			colors->ext_len = len;
		}

		struct ext_color *ext = leaf->value;
		if (ext->case_sensitive) {
			continue;
		}

		// set_ext() already reversed and lowercased the extension
		struct trie_leaf *ileaf;
		while ((ileaf = trie_find_postfix(&colors->iext_trie, ext->ext))) {
			trie_remove(&colors->iext_trie, ileaf);
		}

		ileaf = trie_insert_str(&colors->iext_trie, ext->ext);
		if (!ileaf) {
			return -1;
		}
		ileaf->value = ext;
	}

	return 0;
}

/**
 * Find a color by an extension.
 */
static const struct esc_seq *get_ext(const struct colors *colors, const char *filename) {
	size_t ext_len = colors->ext_len;
	size_t name_len = strlen(filename);
	if (name_len < ext_len) {
		ext_len = name_len;
	}
	const char *suffix = filename + name_len - ext_len;

	char buf[256];
	char *copy;
	if (ext_len < sizeof(buf)) {
		copy = memcpy(buf, suffix, ext_len + 1);
	} else {
		copy = strndup(suffix, ext_len);
		if (!copy) {
			return NULL;
		}
	}

	ext_reverse(copy, ext_len);
	const struct trie_leaf *leaf = trie_find_prefix(&colors->ext_trie, copy);
	const struct ext_color *ext = leaf ? leaf->value : NULL;

	ext_tolower(copy, ext_len);
	const struct trie_leaf *ileaf = trie_find_prefix(&colors->iext_trie, copy);
	const struct ext_color *iext = ileaf ? ileaf->value : NULL;

	if (iext && (!ext || ext->priority < iext->priority)) {
		ext = iext;
	}

	if (copy != buf) {
		free(copy);
	}

	return ext ? ext->esc : NULL;
}

/**
 * Parse a chunk of $LS_COLORS that may have escape sequences.  The supported
 * escapes are:
 *
 * \a, \b, \f, \n, \r, \t, \v:
 *     As in C
 * \e:
 *     ESC (\033)
 * \?:
 *     DEL (\177)
 * \_:
 *     ' ' (space)
 * \NNN:
 *     Octal
 * \xNN:
 *     Hex
 * ^C:
 *     Control character.
 *
 * See man dir_colors.
 *
 * @param str
 *         A dstring to fill with the unescaped chunk.
 * @param value
 *         The value to parse.
 * @param end
 *         The character that marks the end of the chunk.
 * @param[out] next
 *         Will be set to the next chunk.
 * @return
 *         0 on success, -1 on failure.
 */
static int unescape(char **str, const char *value, char end, const char **next) {
	*next = NULL;

	if (!value) {
		errno = EINVAL;
		return -1;
	}

	if (dstresize(str, 0) != 0) {
		return -1;
	}

	const char *i;
	for (i = value; *i && *i != end; ++i) {
		unsigned char c = 0;

		switch (*i) {
		case '\\':
			switch (*++i) {
			case 'a':
				c = '\a';
				break;
			case 'b':
				c = '\b';
				break;
			case 'e':
				c = '\033';
				break;
			case 'f':
				c = '\f';
				break;
			case 'n':
				c = '\n';
				break;
			case 'r':
				c = '\r';
				break;
			case 't':
				c = '\t';
				break;
			case 'v':
				c = '\v';
				break;
			case '?':
				c = '\177';
				break;
			case '_':
				c = ' ';
				break;

			case '0':
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
				while (i[1] >= '0' && i[1] <= '7') {
					c <<= 3;
					c |= *i++ - '0';
				}
				c <<= 3;
				c |= *i - '0';
				break;

			case 'X':
			case 'x':
				while (true) {
					if (i[1] >= '0' && i[1] <= '9') {
						c <<= 4;
						c |= i[1] - '0';
					} else if (i[1] >= 'A' && i[1] <= 'F') {
						c <<= 4;
						c |= i[1] - 'A' + 0xA;
					} else if (i[1] >= 'a' && i[1] <= 'f') {
						c <<= 4;
						c |= i[1] - 'a' + 0xA;
					} else {
						break;
					}
					++i;
				}
				break;

			case '\0':
				errno = EINVAL;
				return -1;

			default:
				c = *i;
				break;
			}
			break;

		case '^':
			switch (*++i) {
			case '?':
				c = '\177';
				break;
			case '\0':
				errno = EINVAL;
				return -1;
			default:
				// CTRL masks bits 6 and 7
				c = *i & 0x1F;
				break;
			}
			break;

		default:
			c = *i;
			break;
		}

		if (dstrapp(str, c) != 0) {
			return -1;
		}
	}

	if (*i) {
		*next = i + 1;
	}

	return 0;
}

/** Parse the GNU $LS_COLORS format. */
static int parse_gnu_ls_colors(struct colors *colors, const char *ls_colors) {
	int ret = -1;
	char *key = NULL;
	char *value = NULL;

	for (const char *chunk = ls_colors, *next; chunk; chunk = next) {
		if (chunk[0] == '*') {
			if (unescape(&key, chunk + 1, '=', &next) != 0) {
				goto fail;
			}
			if (unescape(&value, next, ':', &next) != 0) {
				goto fail;
			}
			if (set_ext(colors, key, value) != 0) {
				goto fail;
			}
		} else {
			const char *equals = strchr(chunk, '=');
			if (!equals) {
				break;
			}

			if (dstrncpy(&key, chunk, equals - chunk) != 0) {
				goto fail;
			}
			if (unescape(&value, equals + 1, ':', &next) != 0) {
				goto fail;
			}

			// All-zero values should be treated like NULL, to fall
			// back on any other relevant coloring for that file
			char *esc = value;
			if (strspn(value, "0") == strlen(value)
			    && strcmp(key, "rs") != 0
			    && strcmp(key, "lc") != 0
			    && strcmp(key, "rc") != 0
			    && strcmp(key, "ec") != 0) {
				esc = NULL;
			}

			if (set_esc(colors, key, esc) != 0) {
				goto fail;
			}
		}
	}

	ret = 0;
fail:
	dstrfree(value);
	dstrfree(key);
	return ret;
}

struct colors *parse_colors(void) {
	struct colors *colors = ALLOC(struct colors);
	if (!colors) {
		return NULL;
	}

	VARENA_INIT(&colors->esc_arena, struct esc_seq, seq);
	VARENA_INIT(&colors->ext_arena, struct ext_color, ext);
	trie_init(&colors->names);
	colors->ext_count = 0;
	colors->ext_len = 0;
	trie_init(&colors->ext_trie);
	trie_init(&colors->iext_trie);

	int ret = 0;

	// From man console_codes

	ret |= init_esc(colors, "rs", "0",      &colors->reset);
	ret |= init_esc(colors, "lc", "\033[",  &colors->leftcode);
	ret |= init_esc(colors, "rc", "m",      &colors->rightcode);
	ret |= init_esc(colors, "ec", NULL,     &colors->endcode);
	ret |= init_esc(colors, "cl", "\033[K", &colors->clear_to_eol);

	ret |= init_esc(colors, "bld", "01;39", &colors->bold);
	ret |= init_esc(colors, "gry", "01;30", &colors->gray);
	ret |= init_esc(colors, "red", "01;31", &colors->red);
	ret |= init_esc(colors, "grn", "01;32", &colors->green);
	ret |= init_esc(colors, "ylw", "01;33", &colors->yellow);
	ret |= init_esc(colors, "blu", "01;34", &colors->blue);
	ret |= init_esc(colors, "mag", "01;35", &colors->magenta);
	ret |= init_esc(colors, "cyn", "01;36", &colors->cyan);
	ret |= init_esc(colors, "wht", "01;37", &colors->white);

	ret |= init_esc(colors, "wrn", "01;33", &colors->warning);
	ret |= init_esc(colors, "err", "01;31", &colors->error);

	// Defaults from man dir_colors
	// "" means fall back to ->normal

	ret |= init_esc(colors, "no", NULL,    &colors->normal);

	ret |= init_esc(colors, "fi", "",      &colors->file);
	ret |= init_esc(colors, "mh", NULL,    &colors->multi_hard);
	ret |= init_esc(colors, "ex", "01;32", &colors->executable);
	ret |= init_esc(colors, "ca", NULL,    &colors->capable);
	ret |= init_esc(colors, "sg", "30;43", &colors->setgid);
	ret |= init_esc(colors, "su", "37;41", &colors->setuid);

	ret |= init_esc(colors, "di", "01;34", &colors->directory);
	ret |= init_esc(colors, "st", "37;44", &colors->sticky);
	ret |= init_esc(colors, "ow", "34;42", &colors->other_writable);
	ret |= init_esc(colors, "tw", "30;42", &colors->sticky_other_writable);

	ret |= init_esc(colors, "ln", "01;36", &colors->link);
	ret |= init_esc(colors, "or", NULL,    &colors->orphan);
	ret |= init_esc(colors, "mi", NULL,    &colors->missing);
	colors->link_as_target = false;

	ret |= init_esc(colors, "bd", "01;33", &colors->blockdev);
	ret |= init_esc(colors, "cd", "01;33", &colors->chardev);
	ret |= init_esc(colors, "do", "01;35", &colors->door);
	ret |= init_esc(colors, "pi", "33",    &colors->pipe);
	ret |= init_esc(colors, "so", "01;35", &colors->socket);

	if (ret != 0) {
		goto fail;
	}

	if (parse_gnu_ls_colors(colors, getenv("LS_COLORS")) != 0) {
		goto fail;
	}
	if (parse_gnu_ls_colors(colors, getenv("BFS_COLORS")) != 0) {
		goto fail;
	}
	if (build_iext_trie(colors) != 0) {
		goto fail;
	}

	if (colors->link && esc_eq(colors->link, "target", strlen("target"))) {
		colors->link_as_target = true;
		colors->link->len = 0;
	}

	return colors;

fail:
	free_colors(colors);
	return NULL;
}

void free_colors(struct colors *colors) {
	if (!colors) {
		return;
	}

	trie_destroy(&colors->iext_trie);
	trie_destroy(&colors->ext_trie);
	trie_destroy(&colors->names);
	varena_destroy(&colors->ext_arena);
	varena_destroy(&colors->esc_arena);

	free(colors);
}

CFILE *cfwrap(FILE *file, const struct colors *colors, bool close) {
	CFILE *cfile = ALLOC(CFILE);
	if (!cfile) {
		return NULL;
	}

	cfile->buffer = dstralloc(128);
	if (!cfile->buffer) {
		free(cfile);
		return NULL;
	}

	cfile->file = file;
	cfile->need_reset = false;
	cfile->close = close;

	if (isatty(fileno(file))) {
		cfile->colors = colors;
	} else {
		cfile->colors = NULL;
	}

	return cfile;
}

int cfclose(CFILE *cfile) {
	int ret = 0;

	if (cfile) {
		dstrfree(cfile->buffer);

		if (cfile->close) {
			ret = fclose(cfile->file);
		}

		free(cfile);
	}

	return ret;
}

/** Check if a symlink is broken. */
static bool is_link_broken(const struct BFTW *ftwbuf) {
	if (ftwbuf->stat_flags & BFS_STAT_NOFOLLOW) {
		return xfaccessat(ftwbuf->at_fd, ftwbuf->at_path, F_OK) != 0;
	} else {
		return true;
	}
}

/** Get the color for a file. */
static const struct esc_seq *file_color(const struct colors *colors, const char *filename, const struct BFTW *ftwbuf, enum bfs_stat_flags flags) {
	enum bfs_type type = bftw_type(ftwbuf, flags);
	if (type == BFS_ERROR) {
		goto error;
	}

	const struct bfs_stat *statbuf = NULL;
	const struct esc_seq *color = NULL;

	switch (type) {
	case BFS_REG:
		if (colors->setuid || colors->setgid || colors->executable || colors->multi_hard) {
			statbuf = bftw_stat(ftwbuf, flags);
			if (!statbuf) {
				goto error;
			}
		}

		if (colors->setuid && (statbuf->mode & 04000)) {
			color = colors->setuid;
		} else if (colors->setgid && (statbuf->mode & 02000)) {
			color = colors->setgid;
		} else if (colors->capable && bfs_check_capabilities(ftwbuf) > 0) {
			color = colors->capable;
		} else if (colors->executable && (statbuf->mode & 00111)) {
			color = colors->executable;
		} else if (colors->multi_hard && statbuf->nlink > 1) {
			color = colors->multi_hard;
		}

		if (!color) {
			color = get_ext(colors, filename);
		}

		if (!color) {
			color = colors->file;
		}

		break;

	case BFS_DIR:
		if (colors->sticky_other_writable || colors->other_writable || colors->sticky) {
			statbuf = bftw_stat(ftwbuf, flags);
			if (!statbuf) {
				goto error;
			}
		}

		if (colors->sticky_other_writable && (statbuf->mode & 01002) == 01002) {
			color = colors->sticky_other_writable;
		} else if (colors->other_writable && (statbuf->mode & 00002)) {
			color = colors->other_writable;
		} else if (colors->sticky && (statbuf->mode & 01000)) {
			color = colors->sticky;
		} else {
			color = colors->directory;
		}

		break;

	case BFS_LNK:
		if (colors->orphan && is_link_broken(ftwbuf)) {
			color = colors->orphan;
		} else {
			color = colors->link;
		}
		break;

	case BFS_BLK:
		color = colors->blockdev;
		break;
	case BFS_CHR:
		color = colors->chardev;
		break;
	case BFS_FIFO:
		color = colors->pipe;
		break;
	case BFS_SOCK:
		color = colors->socket;
		break;
	case BFS_DOOR:
		color = colors->door;
		break;

	default:
		break;
	}

	if (color && color->len == 0) {
		color = colors->normal;
	}

	return color;

error:
	if (colors->missing) {
		return colors->missing;
	} else {
		return colors->orphan;
	}
}

/** Print an escape sequence chunk. */
static int print_esc_chunk(CFILE *cfile, const struct esc_seq *esc) {
	return dstrxcat(&cfile->buffer, esc->seq, esc->len);
}

/** Print an ANSI escape sequence. */
static int print_esc(CFILE *cfile, const struct esc_seq *esc) {
	if (!esc) {
		return 0;
	}

	const struct colors *colors = cfile->colors;
	if (esc != colors->reset) {
		cfile->need_reset = true;
	}

	if (print_esc_chunk(cfile, cfile->colors->leftcode) != 0) {
		return -1;
	}
	if (print_esc_chunk(cfile, esc) != 0) {
		return -1;
	}
	if (print_esc_chunk(cfile, cfile->colors->rightcode) != 0) {
		return -1;
	}

	return 0;
}

/** Reset after an ANSI escape sequence. */
static int print_reset(CFILE *cfile) {
	if (!cfile->need_reset) {
		return 0;
	}
	cfile->need_reset = false;

	const struct colors *colors = cfile->colors;
	if (colors->endcode) {
		return print_esc_chunk(cfile, colors->endcode);
	} else {
		return print_esc(cfile, colors->reset);
	}
}

/** Print a shell-escaped string. */
static int print_wordesc(CFILE *cfile, const char *str, size_t n, enum wesc_flags flags) {
	return dstrnescat(&cfile->buffer, str, n, flags);
}

/** Print a string with an optional color. */
static int print_colored(CFILE *cfile, const struct esc_seq *esc, const char *str, size_t len) {
	if (print_esc(cfile, esc) != 0) {
		return -1;
	}
	if (dstrxcat(&cfile->buffer, str, len) != 0) {
		return -1;
	}
	if (print_reset(cfile) != 0) {
		return -1;
	}

	return 0;
}

/** Find the offset of the first broken path component. */
static ssize_t first_broken_offset(const char *path, const struct BFTW *ftwbuf, enum bfs_stat_flags flags, size_t max) {
	ssize_t ret = max;
	bfs_assert(ret >= 0);

	if (bftw_type(ftwbuf, flags) != BFS_ERROR) {
		goto out;
	}

	char *at_path;
	int at_fd;
	if (path == ftwbuf->path) {
		if (ftwbuf->depth == 0) {
			at_fd = AT_FDCWD;
			at_path = dstrndup(path, max);
		} else {
			// The parent must have existed to get here
			goto out;
		}
	} else {
		// We're in print_link_target(), so resolve relative to the link's parent directory
		at_fd = ftwbuf->at_fd;
		if (at_fd == AT_FDCWD && path[0] != '/') {
			at_path = dstrndup(ftwbuf->path, ftwbuf->nameoff);
			if (at_path && dstrncat(&at_path, path, max) != 0) {
				ret = -1;
				goto out_path;
			}
		} else {
			at_path = dstrndup(path, max);
		}
	}

	if (!at_path) {
		ret = -1;
		goto out;
	}

	while (ret > 0) {
		if (xfaccessat(at_fd, at_path, F_OK) == 0) {
			break;
		}

		size_t len = dstrlen(at_path);
		while (ret && at_path[len - 1] == '/') {
			--len, --ret;
		}
		while (ret && at_path[len - 1] != '/') {
			--len, --ret;
		}

		dstresize(&at_path, len);
	}

out_path:
	dstrfree(at_path);
out:
	return ret;
}

/** Print the directories leading up to a file. */
static int print_dirs_colored(CFILE *cfile, const char *path, const struct BFTW *ftwbuf, enum bfs_stat_flags flags, size_t nameoff) {
	const struct colors *colors = cfile->colors;

	ssize_t broken = first_broken_offset(path, ftwbuf, flags, nameoff);
	if (broken < 0) {
		return -1;
	}

	if (broken > 0) {
		if (print_colored(cfile, colors->directory, path, broken) != 0) {
			return -1;
		}
	}

	if ((size_t)broken < nameoff) {
		const struct esc_seq *color = colors->missing;
		if (!color) {
			color = colors->orphan;
		}
		if (print_colored(cfile, color, path + broken, nameoff - broken) != 0) {
			return -1;
		}
	}

	return 0;
}

/** Print a file name with colors. */
static int print_name_colored(CFILE *cfile, const char *name, const struct BFTW *ftwbuf, enum bfs_stat_flags flags) {
	const struct esc_seq *esc = file_color(cfile->colors, name, ftwbuf, flags);
	return print_colored(cfile, esc, name, strlen(name));
}

/** Print a path with colors. */
static int print_path_colored(CFILE *cfile, const char *path, const struct BFTW *ftwbuf, enum bfs_stat_flags flags) {
	size_t nameoff;
	if (path == ftwbuf->path) {
		nameoff = ftwbuf->nameoff;
	} else {
		nameoff = xbaseoff(path);
	}

	if (print_dirs_colored(cfile, path, ftwbuf, flags, nameoff) != 0) {
		return -1;
	}

	return print_name_colored(cfile, path + nameoff, ftwbuf, flags);
}

/** Print the name of a file with the appropriate colors. */
static int print_name(CFILE *cfile, const struct BFTW *ftwbuf) {
	const char *name = ftwbuf->path + ftwbuf->nameoff;

	const struct colors *colors = cfile->colors;
	if (!colors) {
		return dstrcat(&cfile->buffer, name);
	}

	enum bfs_stat_flags flags = ftwbuf->stat_flags;
	if (colors->link_as_target && ftwbuf->type == BFS_LNK) {
		flags = BFS_STAT_TRYFOLLOW;
	}

	return print_name_colored(cfile, name, ftwbuf, flags);
}

/** Print the path to a file with the appropriate colors. */
static int print_path(CFILE *cfile, const struct BFTW *ftwbuf) {
	const struct colors *colors = cfile->colors;
	if (!colors) {
		return dstrcat(&cfile->buffer, ftwbuf->path);
	}

	enum bfs_stat_flags flags = ftwbuf->stat_flags;
	if (colors->link_as_target && ftwbuf->type == BFS_LNK) {
		flags = BFS_STAT_TRYFOLLOW;
	}

	return print_path_colored(cfile, ftwbuf->path, ftwbuf, flags);
}

/** Print a link target with the appropriate colors. */
static int print_link_target(CFILE *cfile, const struct BFTW *ftwbuf) {
	const struct bfs_stat *statbuf = bftw_cached_stat(ftwbuf, BFS_STAT_NOFOLLOW);
	size_t len = statbuf ? statbuf->size : 0;

	char *target = xreadlinkat(ftwbuf->at_fd, ftwbuf->at_path, len);
	if (!target) {
		return -1;
	}

	int ret;
	if (cfile->colors) {
		ret = print_path_colored(cfile, target, ftwbuf, BFS_STAT_FOLLOW);
	} else {
		ret = dstrcat(&cfile->buffer, target);
	}

	free(target);
	return ret;
}

/** Format some colored output to the buffer. */
BFS_FORMATTER(2, 3)
static int cbuff(CFILE *cfile, const char *format, ...);

/** Dump a parsed expression tree, for debugging. */
static int print_expr(CFILE *cfile, const struct bfs_expr *expr, bool verbose) {
	if (!expr) {
		return dstrcat(&cfile->buffer, "(null)");
	}

	if (dstrcat(&cfile->buffer, "(") != 0) {
		return -1;
	}

	const struct bfs_expr *lhs = NULL;
	const struct bfs_expr *rhs = NULL;

	if (bfs_expr_is_parent(expr)) {
		lhs = expr->lhs;
		rhs = expr->rhs;

		if (cbuff(cfile, "${red}%pq${rs}", expr->argv[0]) < 0) {
			return -1;
		}
	} else {
		if (cbuff(cfile, "${blu}%pq${rs}", expr->argv[0]) < 0) {
			return -1;
		}
	}

	for (size_t i = 1; i < expr->argc; ++i) {
		if (cbuff(cfile, " ${bld}%pq${rs}", expr->argv[i]) < 0) {
			return -1;
		}
	}

	if (verbose) {
		double rate = 0.0, time = 0.0;
		if (expr->evaluations) {
			rate = 100.0*expr->successes/expr->evaluations;
			time = (1.0e9*expr->elapsed.tv_sec + expr->elapsed.tv_nsec)/expr->evaluations;
		}
		if (cbuff(cfile, " [${ylw}%zu${rs}/${ylw}%zu${rs}=${ylw}%g%%${rs}; ${ylw}%gns${rs}]",
		          expr->successes, expr->evaluations, rate, time)) {
			return -1;
		}
	}

	if (lhs) {
		if (dstrcat(&cfile->buffer, " ") != 0) {
			return -1;
		}
		if (print_expr(cfile, lhs, verbose) != 0) {
			return -1;
		}
	}

	if (rhs) {
		if (dstrcat(&cfile->buffer, " ") != 0) {
			return -1;
		}
		if (print_expr(cfile, rhs, verbose) != 0) {
			return -1;
		}
	}

	if (dstrcat(&cfile->buffer, ")") != 0) {
		return -1;
	}

	return 0;
}

BFS_FORMATTER(2, 0)
static int cvbuff(CFILE *cfile, const char *format, va_list args) {
	const struct colors *colors = cfile->colors;
	int error = errno;

	// Color specifier (e.g. ${blu}) state
	struct esc_seq **esc;
	const char *end;
	size_t len;
	char name[4];

	for (const char *i = format; *i; ++i) {
		size_t verbatim = strcspn(i, "%$");
		if (dstrncat(&cfile->buffer, i, verbatim) != 0) {
			return -1;
		}
		i += verbatim;

		switch (*i) {
		case '%':
			switch (*++i) {
			case '%':
				if (dstrapp(&cfile->buffer, '%') != 0) {
					return -1;
				}
				break;

			case 'c':
				if (dstrapp(&cfile->buffer, va_arg(args, int)) != 0) {
					return -1;
				}
				break;

			case 'd':
				if (dstrcatf(&cfile->buffer, "%d", va_arg(args, int)) != 0) {
					return -1;
				}
				break;

			case 'g':
				if (dstrcatf(&cfile->buffer, "%g", va_arg(args, double)) != 0) {
					return -1;
				}
				break;

			case 's':
				if (dstrcat(&cfile->buffer, va_arg(args, const char *)) != 0) {
					return -1;
				}
				break;

			case 'z':
				++i;
				if (*i != 'u') {
					goto invalid;
				}
				if (dstrcatf(&cfile->buffer, "%zu", va_arg(args, size_t)) != 0) {
					return -1;
				}
				break;

			case 'm':
				if (dstrcat(&cfile->buffer, strerror(error)) != 0) {
					return -1;
				}
				break;

			case 'p':
				switch (*++i) {
				case 'q':
					if (print_wordesc(cfile, va_arg(args, const char *), SIZE_MAX, WESC_SHELL | WESC_TTY) != 0) {
						return -1;
					}
					break;

				case 'F':
					if (print_name(cfile, va_arg(args, const struct BFTW *)) != 0) {
						return -1;
					}
					break;

				case 'P':
					if (print_path(cfile, va_arg(args, const struct BFTW *)) != 0) {
						return -1;
					}
					break;

				case 'L':
					if (print_link_target(cfile, va_arg(args, const struct BFTW *)) != 0) {
						return -1;
					}
					break;

				case 'e':
					if (print_expr(cfile, va_arg(args, const struct bfs_expr *), false) != 0) {
						return -1;
					}
					break;
				case 'E':
					if (print_expr(cfile, va_arg(args, const struct bfs_expr *), true) != 0) {
						return -1;
					}
					break;

				default:
					goto invalid;
				}

				break;

			default:
				goto invalid;
			}
			break;

		case '$':
			switch (*++i) {
			case '$':
				if (dstrapp(&cfile->buffer, '$') != 0) {
					return -1;
				}
				break;

			case '{':
				++i;
				end = strchr(i, '}');
				if (!end) {
					goto invalid;
				}
				if (!colors) {
					i = end;
					break;
				}

				len = end - i;
				if (len >= sizeof(name)) {
					goto invalid;
				}
				memcpy(name, i, len);
				name[len] = '\0';

				if (strcmp(name, "rs") == 0) {
					if (print_reset(cfile) != 0) {
						return -1;
					}
				} else {
					esc = get_esc(colors, name);
					if (!esc) {
						goto invalid;
					}
					if (print_esc(cfile, *esc) != 0) {
						return -1;
					}
				}

				i = end;
				break;

			default:
				goto invalid;
			}
			break;

		default:
			return 0;
		}
	}

	return 0;

invalid:
	bfs_bug("Invalid format string '%s'", format);
	errno = EINVAL;
	return -1;
}

static int cbuff(CFILE *cfile, const char *format, ...) {
	va_list args;
	va_start(args, format);
	int ret = cvbuff(cfile, format, args);
	va_end(args);
	return ret;
}

int cvfprintf(CFILE *cfile, const char *format, va_list args) {
	bfs_assert(dstrlen(cfile->buffer) == 0);

	int ret = -1;
	if (cvbuff(cfile, format, args) == 0) {
		size_t len = dstrlen(cfile->buffer);
		if (fwrite(cfile->buffer, 1, len, cfile->file) == len) {
			ret = 0;
		}
	}

	dstresize(&cfile->buffer, 0);
	return ret;
}

int cfprintf(CFILE *cfile, const char *format, ...) {
	va_list args;
	va_start(args, format);
	int ret = cvfprintf(cfile, format, args);
	va_end(args);
	return ret;
}
