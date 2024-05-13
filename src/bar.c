// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include "prelude.h"
#include "bar.h"
#include "atomic.h"
#include "bfstd.h"
#include "bit.h"
#include "diag.h"
#include "dstring.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>

/**
 * Sentinel values for bfs_bar::refcount.
 */
enum {
	/** The bar is currently uninitialized. */
	BFS_BAR_UNINIT = 0,
	/** The bar is being initialized/destroyed. */
	BFS_BAR_BUSY   = 1,
	/** Values >= this are valid refcounts. */
	BFS_BAR_READY  = 2,
};

struct bfs_bar {
	/**
	 * A file descriptor to the TTY.  Must be atomic because signal handlers
	 * can race with bfs_bar_hide().
	 */
	atomic int fd;
	/** A reference count protecting `fd`. */
	atomic unsigned int refcount;

	/** The width of the TTY. */
	atomic unsigned int width;
	/** The height of the TTY. */
	atomic unsigned int height;
};

/** The global status bar instance. */
static struct bfs_bar the_bar = {
	.fd = -1,
};

/** Try to acquire a reference to bar->fd. */
static int bfs_bar_getfd(struct bfs_bar *bar) {
	unsigned int refs = load(&bar->refcount, relaxed);
	do {
		if (refs < BFS_BAR_READY) {
			errno = EAGAIN;
			return -1;
		}
	} while (!compare_exchange_weak(&bar->refcount, &refs, refs + 1, acquire, relaxed));

	return load(&bar->fd, relaxed);
}

/** Release a reference to bar->fd. */
static void bfs_bar_putfd(struct bfs_bar *bar) {
	int fd = load(&bar->fd, relaxed);

	unsigned int refs = fetch_sub(&bar->refcount, 1, release);
	bfs_assert(refs >= BFS_BAR_READY);

	if (refs == 2) {
		close_quietly(fd);
		store(&bar->fd, -1, relaxed);
		store(&bar->refcount, BFS_BAR_UNINIT, release);
	}
}

/** Get the terminal size, if possible. */
static int bfs_bar_getsize(struct bfs_bar *bar) {
#ifdef TIOCGWINSZ
	int fd = bfs_bar_getfd(bar);
	if (fd < 0) {
		return -1;
	}

	struct winsize ws;
	int ret = ioctl(fd, TIOCGWINSZ, &ws);
	if (ret == 0) {
		store(&bar->width, ws.ws_col, relaxed);
		store(&bar->height, ws.ws_row, relaxed);
	}

	bfs_bar_putfd(bar);
	return ret;
#else
	errno = ENOTSUP;
	return -1;
#endif
}

/** Write a string to the status bar (async-signal-safe). */
static int bfs_bar_write(struct bfs_bar *bar, const char *str, size_t len) {
	int fd = bfs_bar_getfd(bar);
	if (fd < 0) {
		return -1;
	}

	int ret = xwrite(fd, str, len) == len ? 0 : -1;
	bfs_bar_putfd(bar);
	return ret;
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
static void sighand_winch(int sig) {
	int error = errno;

	bfs_bar_getsize(&the_bar);
	bfs_bar_resize(&the_bar);

	errno = error;
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

	int ret = bfs_bar_write(bar, str, dstrlen(str));
	dstrfree(str);
	return ret;
}

struct bfs_bar *bfs_bar_show(void) {
	unsigned int refs = BFS_BAR_UNINIT;
	if (!compare_exchange_strong(&the_bar.refcount, &refs, BFS_BAR_BUSY, acq_rel, relaxed)) {
		errno = EBUSY;
		goto fail;
	}

	char term[L_ctermid];
	ctermid(term);
	if (strlen(term) == 0) {
		errno = ENOTTY;
		goto unref;
	}

	int fd = open(term, O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		goto unref;
	}
	store(&the_bar.fd, fd, relaxed);
	store(&the_bar.refcount, BFS_BAR_READY, release);

	if (bfs_bar_getsize(&the_bar) != 0) {
		goto put;
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

	bfs_bar_resize(&the_bar);
	return &the_bar;

put:
	bfs_bar_putfd(&the_bar);
	return NULL;
unref:
	store(&the_bar.refcount, 0, release);
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
	bfs_bar_putfd(bar);
}
