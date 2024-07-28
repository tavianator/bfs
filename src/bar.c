// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include "prelude.h"
#include "bar.h"
#include "alloc.h"
#include "atomic.h"
#include "bfstd.h"
#include "bit.h"
#include "dstring.h"
#include "sighook.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>

struct bfs_bar {
	int fd;
	atomic unsigned int width;
	atomic unsigned int height;

	struct sighook *exit_hook;
	struct sighook *winch_hook;
};

/** Get the terminal size, if possible. */
static int bfs_bar_getsize(struct bfs_bar *bar) {
#ifdef TIOCGWINSZ
	struct winsize ws;
	if (ioctl(bar->fd, TIOCGWINSZ, &ws) != 0) {
		return -1;
	}

	store(&bar->width, ws.ws_col, relaxed);
	store(&bar->height, ws.ws_row, relaxed);
	return 0;
#else
	errno = ENOTSUP;
	return -1;
#endif
}

/** Write a string to the status bar (async-signal-safe). */
static int bfs_bar_write(struct bfs_bar *bar, const char *str, size_t len) {
	return xwrite(bar->fd, str, len) == len ? 0 : -1;
}

/** Write a string to the status bar (async-signal-safe). */
static int bfs_bar_puts(struct bfs_bar *bar, const char *str) {
	return bfs_bar_write(bar, str, strlen(str));
}

/** Number of decimal digits needed for terminal sizes. */
#define ITOA_DIGITS ((USHRT_WIDTH + 2) / 3)

/** Async Signal Safe itoa(). */
static char *ass_itoa(char *str, unsigned int n) {
	char *end = str + ITOA_DIGITS;
	*end = '\0';

	char *c = end;
	do {
		*--c = '0' + (n % 10);
		n /= 10;
	} while (n);

	size_t len = end - c;
	memmove(str, c, len + 1);
	return str + len;
}

/** Update the size of the scrollable region. */
static int bfs_bar_resize(struct bfs_bar *bar) {
	static const char PREFIX[] =
		"\033D"   // IND: Line feed, possibly scrolling
		"\033[1A" // CUU: Move cursor up 1 row
		"\0337"   // DECSC: Save cursor
		"\033[;"; // DECSTBM: Set scrollable region
	static const char SUFFIX[] =
		"r"       // (end of DECSTBM)
		"\0338"   // DECRC: Restore the cursor
		"\033[J"; // ED: Erase display from cursor to end

	char esc_seq[sizeof(PREFIX) + ITOA_DIGITS + sizeof(SUFFIX)];

	// DECSTBM takes the height as the second argument
	unsigned int height = load(&bar->height, relaxed) - 1;

	char *cur = stpcpy(esc_seq, PREFIX);
	cur = ass_itoa(cur, height);
	cur = stpcpy(cur, SUFFIX);

	return bfs_bar_write(bar, esc_seq, cur - esc_seq);
}

#ifdef SIGWINCH
/** SIGWINCH handler. */
static void bfs_bar_sigwinch(int sig, siginfo_t *info, void *arg) {
	struct bfs_bar *bar = arg;
	bfs_bar_getsize(bar);
	bfs_bar_resize(bar);
}
#endif

/** Reset the scrollable region and hide the bar. */
static int bfs_bar_reset(struct bfs_bar *bar) {
	return bfs_bar_puts(bar,
		"\0337"  // DECSC: Save cursor
		"\033[r" // DECSTBM: Reset scrollable region
		"\0338"  // DECRC: Restore cursor
		"\033[J" // ED: Erase display from cursor to end
	);
}

/** Signal handler for process-terminating signals. */
static void bfs_bar_sigexit(int sig, siginfo_t *info, void *arg) {
	struct bfs_bar *bar = arg;
	bfs_bar_reset(bar);
}

/** printf() to the status bar with a single write(). */
_printf(2, 3)
static int bfs_bar_printf(struct bfs_bar *bar, const char *format, ...) {
	va_list args;
	va_start(args, format);
	dchar *str = dstrvprintf(format, args);
	va_end(args);

	if (!str) {
		return -1;
	}

	int ret = bfs_bar_write(bar, str, dstrlen(str));
	dstrfree(str);
	return ret;
}

struct bfs_bar *bfs_bar_show(void) {
	struct bfs_bar *bar = ALLOC(struct bfs_bar);
	if (!bar) {
		return NULL;
	}

	bar->fd = open_cterm(O_RDWR | O_CLOEXEC);
	if (bar->fd < 0) {
		goto fail;
	}

	if (bfs_bar_getsize(bar) != 0) {
		goto fail_close;
	}

	bar->exit_hook = atsigexit(bfs_bar_sigexit, bar);
	if (!bar->exit_hook) {
		goto fail_close;
	}

#ifdef SIGWINCH
	bar->winch_hook = sighook(SIGWINCH, bfs_bar_sigwinch, bar, 0);
	if (!bar->winch_hook) {
		goto fail_hook;
	}
#endif

	bfs_bar_resize(bar);
	return bar;

fail_hook:
	sigunhook(bar->exit_hook);
fail_close:
	close_quietly(bar->fd);
fail:
	free(bar);
	return NULL;
}

unsigned int bfs_bar_width(const struct bfs_bar *bar) {
	return load(&bar->width, relaxed);
}

int bfs_bar_update(struct bfs_bar *bar, const char *str) {
	unsigned int height = load(&bar->height, relaxed);
	return bfs_bar_printf(bar,
		"\0337"      // DECSC: Save cursor
		"\033[%u;0f" // HVP: Move cursor to row, column
		"\033[K"     // EL: Erase line
		"\033[7m"    // SGR reverse video
		"%s"
		"\033[27m"   // SGR reverse video off
		"\0338",     // DECRC: Restore cursor
		height,
		str
	);
}

void bfs_bar_hide(struct bfs_bar *bar) {
	if (!bar) {
		return;
	}

	sigunhook(bar->winch_hook);
	sigunhook(bar->exit_hook);

	bfs_bar_reset(bar);

	xclose(bar->fd);
	free(bar);
}
