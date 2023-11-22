// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include "bar.h"
#include "atomic.h"
#include "bfstd.h"
#include "bit.h"
#include "config.h"
#include "dstring.h"
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
};

/** The global status bar instance. */
static struct bfs_bar the_bar = {
	.fd = -1,
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

/** Async Signal Safe puts(). */
static int ass_puts(int fd, const char *str) {
	size_t len = strlen(str);
	return xwrite(fd, str, len) == len ? 0 : -1;
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
	char esc_seq[12 + ITOA_DIGITS] =
		"\0337"   // DECSC: Save cursor
		"\033[;"; // DECSTBM: Set scrollable region

	// DECSTBM takes the height as the second argument
	unsigned int height = load(&bar->height, relaxed);
	char *ptr = esc_seq + strlen(esc_seq);
	ptr = ass_itoa(ptr, height - 1);

	strcpy(ptr,
		"r"      // DECSTBM
		"\0338"  // DECRC: Restore the cursor
		"\033[J" // ED: Erase display from cursor to end
	);

	return ass_puts(bar->fd, esc_seq);
}

#ifdef SIGWINCH
/** SIGWINCH handler. */
static void sighand_winch(int sig) {
	int error = errno;

	bfs_bar_getsize(&the_bar);
	bfs_bar_resize(&the_bar);

	errno = error;
}
#endif

/** Reset the scrollable region and hide the bar. */
static int bfs_bar_reset(struct bfs_bar *bar) {
	return ass_puts(bar->fd,
		"\0337"  // DECSC: Save cursor
		"\033[r" // DECSTBM: Reset scrollable region
		"\0338"  // DECRC: Restore cursor
		"\033[J" // ED: Erase display from cursor to end
	);
}

/** Signal handler for process-terminating signals. */
static void sighand_reset(int sig) {
	bfs_bar_reset(&the_bar);
	raise(sig);
}

/** Register sighand_reset() for a signal. */
static void reset_before_death_by(int sig) {
	struct sigaction sa = {
		.sa_handler = sighand_reset,
		.sa_flags = SA_RESETHAND,
	};
	sigemptyset(&sa.sa_mask);
	sigaction(sig, &sa, NULL);
}

/** printf() to the status bar with a single write(). */
attr(printf(2, 3))
static int bfs_bar_printf(struct bfs_bar *bar, const char *format, ...) {
	va_list args;
	va_start(args, format);
	dchar *str = dstrvprintf(format, args);
	va_end(args);

	if (!str) {
		return -1;
	}

	int ret = ass_puts(bar->fd, str);
	dstrfree(str);
	return ret;
}

struct bfs_bar *bfs_bar_show(void) {
	if (the_bar.fd >= 0) {
		errno = EBUSY;
		goto fail;
	}

	char term[L_ctermid];
	ctermid(term);
	if (strlen(term) == 0) {
		errno = ENOTTY;
		goto fail;
	}

	the_bar.fd = open(term, O_RDWR | O_CLOEXEC);
	if (the_bar.fd < 0) {
		goto fail;
	}

	if (bfs_bar_getsize(&the_bar) != 0) {
		goto fail_close;
	}

	reset_before_death_by(SIGABRT);
	reset_before_death_by(SIGINT);
	reset_before_death_by(SIGPIPE);
	reset_before_death_by(SIGQUIT);
	reset_before_death_by(SIGTERM);

#ifdef SIGWINCH
	struct sigaction sa = {
		.sa_handler = sighand_winch,
		.sa_flags = SA_RESTART,
	};
	sigemptyset(&sa.sa_mask);
	sigaction(SIGWINCH, &sa, NULL);
#endif

	unsigned int height = load(&the_bar.height, relaxed);
	bfs_bar_printf(&the_bar,
		"\n"        // Make space for the bar
		"\0337"     // DECSC: Save cursor
		"\033[;%ur" // DECSTBM: Set scrollable region
		"\0338"     // DECRC: Restore cursor
		"\033[1A",  // CUU: Move cursor up 1 row
		height - 1
	);

	return &the_bar;

fail_close:
	close_quietly(the_bar.fd);
	the_bar.fd = -1;
fail:
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

	signal(SIGABRT, SIG_DFL);
	signal(SIGINT, SIG_DFL);
	signal(SIGPIPE, SIG_DFL);
	signal(SIGQUIT, SIG_DFL);
	signal(SIGTERM, SIG_DFL);
#ifdef SIGWINCH
	signal(SIGWINCH, SIG_DFL);
#endif

	bfs_bar_reset(bar);

	xclose(bar->fd);
	bar->fd = -1;
}
