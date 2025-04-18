// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

/**
 * Execute a command in a pseudo-terminal.
 *
 *     $ ptyx [-w WIDTH] [-h HEIGHT] [--] COMMAND [ARGS...]
 */

#include "bfs.h"
#include "bfstd.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#if __has_include(<stropts.h>)
#  include <stropts.h>
#endif

#if __sun
/**
 * Push a STREAMS module, if it's not already there.
 *
 * See https://www.illumos.org/issues/9042.
 */
static int i_push(int fd, const char *name) {
	int ret = ioctl(fd, I_FIND, name);
	if (ret < 0) {
		return ret;
	} else if (ret == 0) {
		return ioctl(fd, I_PUSH, name);
	} else {
		return 0;
	}
}
#endif

int main(int argc, char *argv[]) {
	const char *cmd = argc > 0 ? argv[0] : "ptyx";

/** Report an error message and exit. */
#define die(...) die_(__VA_ARGS__, )

#define die_(format, ...) \
	do { \
		fprintf(stderr, "%s: " format "%s", cmd, __VA_ARGS__ "\n"); \
		exit(EXIT_FAILURE); \
	} while (0)

/** Report an error code and exit. */
#define edie(...) edie_(__VA_ARGS__, )

#define edie_(format, ...) \
	do { \
		fprintf(stderr, "%s: " format ": %s\n", cmd, __VA_ARGS__ errstr()); \
		exit(EXIT_FAILURE); \
	} while (0)

	unsigned short width = 0;
	unsigned short height = 0;

	// Parse the command line
	int c;
	while (c = getopt(argc, argv, "+:w:h:"), c != -1) {
		switch (c) {
		case 'w':
			if (xstrtous(optarg, NULL, 10, &width) != 0) {
				edie("Bad width '%s'", optarg);
			}
			break;
		case 'h':
			if (xstrtous(optarg, NULL, 10, &height) != 0) {
				edie("Bad height '%s'", optarg);
			}
			break;
		case ':':
			die("Missing argument to -%c", optopt);
		case '?':
			die("Unrecognized option -%c", optopt);
		}
	}

	if (optind >= argc) {
		die("Missing command");
	}
	char **args = argv + optind;

	// Create a new pty, and set it up
	int ptm = posix_openpt(O_RDWR | O_NOCTTY);
	if (ptm < 0) {
		edie("posix_openpt()");
	}
	if (grantpt(ptm) != 0) {
		edie("grantpt()");
	}
	if (unlockpt(ptm) != 0) {
		edie("unlockpt()");
	}

	// Get the subsidiary device path
	char *name = ptsname(ptm);
	if (!name) {
		edie("ptsname()");
	}

	// Open the subsidiary device
	int pts = open(name, O_RDWR | O_NOCTTY);
	if (pts < 0) {
		edie("%s", name);
	}

#if __sun
	// On Solaris/illumos, a pty doesn't behave like a terminal until we
	// push some STREAMS modules (see ptm(4D), ptem(4M), ldterm(4M)).
	if (i_push(pts, "ptem") != 0) {
		die("ioctl(I_PUSH, ptem)");
	}
	if (i_push(pts, "ldterm") != 0) {
		die("ioctl(I_PUSH, ldterm)");
	}
#endif

	// A new pty starts at 0x0, which is not very useful.  Instead, grab the
	// default size from the current controlling terminal, if possible.
	if (!width || !height) {
		int tty = open_cterm(O_RDONLY | O_CLOEXEC);
		if (tty >= 0) {
			struct winsize ws;
			if (xtcgetwinsize(tty, &ws) != 0) {
				edie("tcgetwinsize()");
			}
			if (!width) {
				width = ws.ws_col;
			}
			if (!height) {
				height = ws.ws_row;
			}
			xclose(tty);
		}
	}
	if (!width) {
		width = 80;
	}
	if (!height) {
		height = 24;
	}

	// Update the pty size
	struct winsize ws;
	if (xtcgetwinsize(pts, &ws) != 0) {
		edie("tcgetwinsize()");
	}
	ws.ws_col = width;
	ws.ws_row = height;
	if (xtcsetwinsize(pts, &ws) != 0) {
		edie("tcsetwinsize()");
	}

	// Set custom terminal attributes
	struct termios attrs;
	if (tcgetattr(pts, &attrs) != 0) {
		edie("tcgetattr()");
	}
	attrs.c_oflag &= ~OPOST; // Don't convert \n to \r\n
	if (tcsetattr(pts, TCSANOW, &attrs) != 0) {
		edie("tcsetattr()");
	}

	pid_t pid = fork();
	if (pid < 0) {
		edie("fork()");
	} else if (pid == 0) {
		// Child
		close(ptm);

		// Make ourselves a session leader so we can have our own
		// controlling terminal
		if (setsid() < 0) {
			edie("setsid()");
		}

#ifdef TIOCSCTTY
		// Set the pty as the controlling terminal
		if (ioctl(pts, TIOCSCTTY, 0) != 0) {
			edie("ioctl(TIOCSCTTY)");
		}
#endif

		// Redirect std{in,out,err} to the pty
		if (dup2(pts, STDIN_FILENO) < 0
		    || dup2(pts, STDOUT_FILENO) < 0
		    || dup2(pts, STDERR_FILENO) < 0) {
			edie("dup2()");
		}
		if (pts > STDERR_FILENO) {
			xclose(pts);
		}

		// Run the requested command
		execvp(args[0], args);
		edie("execvp(): %s", args[0]);
	}

	// Parent
	xclose(pts);

	// Read output from the pty and copy it to stdout
	char buf[1024];
	while (true) {
		ssize_t len = read(ptm, buf, sizeof(buf));
		if (len > 0) {
			if (xwrite(STDOUT_FILENO, buf, len) < 0) {
				edie("write()");
			}
		} else if (len == 0) {
			break;
		} else if (errno == EINTR) {
			continue;
		} else if (errno == EIO) {
			// Linux reports EIO rather than EOF when pts is closed
			break;
		} else {
			die("read()");
		}
	}

	xclose(ptm);

	int wstatus;
	if (xwaitpid(pid, &wstatus, 0) < 0) {
		edie("waitpid()");
	}

	if (WIFEXITED(wstatus)) {
		return WEXITSTATUS(wstatus);
	} else if (WIFSIGNALED(wstatus)) {
		int sig = WTERMSIG(wstatus);
		fprintf(stderr, "%s: %s: %s\n", cmd, args[0], strsignal(sig));
		return 128 + sig;
	} else {
		return 128;
	}
}
