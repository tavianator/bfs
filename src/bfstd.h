// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

/**
 * Standard library wrappers and polyfills.
 */

#ifndef BFS_BFSTD_H
#define BFS_BFSTD_H

#include "bfs.h"

#include <stddef.h>

#include <ctype.h>

/**
 * Work around https://github.com/llvm/llvm-project/issues/65532 by forcing a
 * function, not a macro, to be called.
 */
#if __FreeBSD__ && __SANITIZE_MEMORY__
#  define BFS_INTERCEPT(fn) (fn)
#else
#  define BFS_INTERCEPT(fn) fn
#endif

/**
 * Wrap isalpha()/isdigit()/etc.
 */
#define BFS_ISCTYPE(fn, c) BFS_INTERCEPT(fn)((unsigned char)(c))

#define xisalnum(c) BFS_ISCTYPE(isalnum, c)
#define xisalpha(c) BFS_ISCTYPE(isalpha, c)
#define xisascii(c) BFS_ISCTYPE(isascii, c)
#define xiscntrl(c) BFS_ISCTYPE(iscntrl, c)
#define xisdigit(c) BFS_ISCTYPE(isdigit, c)
#define xislower(c) BFS_ISCTYPE(islower, c)
#define xisgraph(c) BFS_ISCTYPE(isgraph, c)
#define xisprint(c) BFS_ISCTYPE(isprint, c)
#define xispunct(c) BFS_ISCTYPE(ispunct, c)
#define xisspace(c) BFS_ISCTYPE(isspace, c)
#define xisupper(c) BFS_ISCTYPE(isupper, c)
#define xisxdigit(c) BFS_ISCTYPE(isxdigit, c)

// #include <errno.h>

/**
 * Check if an error code is "like" another one.  For example, ENOTDIR is
 * like ENOENT because they can both be triggered by non-existent paths.
 *
 * @error
 *         The error code to check.
 * @category
 *         The category to test for.  Known categories include ENOENT and
 *         ENAMETOOLONG.
 * @return
 *         Whether the error belongs to the given category.
 */
bool error_is_like(int error, int category);

/**
 * Equivalent to error_is_like(errno, category).
 */
bool errno_is_like(int category);

/**
 * Apply the "negative errno" convention.
 *
 * @ret
 *         The return value of the attempted operation.
 * @return
 *         ret, if non-negative, otherwise -errno.
 */
int try(int ret);

#include <fcntl.h>

#ifndef O_EXEC
#  ifdef O_PATH
#    define O_EXEC O_PATH
#  else
#    define O_EXEC O_RDONLY
#  endif
#endif

#ifndef O_SEARCH
#  ifdef O_PATH
#    define O_SEARCH O_PATH
#  else
#    define O_SEARCH O_RDONLY
#  endif
#endif

#ifndef O_DIRECTORY
#  define O_DIRECTORY 0
#endif

#include <fnmatch.h>

#if !defined(FNM_CASEFOLD) && defined(FNM_IGNORECASE)
#  define FNM_CASEFOLD FNM_IGNORECASE
#endif

// #include <libgen.h>

/**
 * Re-entrant dirname() variant that always allocates a copy.
 *
 * @path
 *         The path in question.
 * @return
 *         The parent directory of the path.
 */
char *xdirname(const char *path);

/**
 * Re-entrant basename() variant that always allocates a copy.
 *
 * @path
 *         The path in question.
 * @return
 *         The final component of the path.
 */
char *xbasename(const char *path);

/**
 * Find the offset of the final component of a path.
 *
 * @path
 *         The path in question.
 * @return
 *         The offset of the basename.
 */
size_t xbaseoff(const char *path);

#include <stdio.h>

/**
 * fopen() variant that takes open() style flags.
 *
 * @path
 *         The path to open.
 * @flags
 *         Flags to pass to open().
 */
FILE *xfopen(const char *path, int flags);

/**
 * Convenience wrapper for getdelim().
 *
 * @file
 *         The file to read.
 * @delim
 *         The delimiter character to split on.
 * @return
 *         The read chunk (without the delimiter), allocated with malloc().
 *         NULL is returned on error (errno != 0) or end of file (errno == 0).
 */
char *xgetdelim(FILE *file, char delim);

// #include <stdlib.h>

/**
 * Wrapper for getprogname() or equivalent functionality.
 *
 * @return
 *         The basename of the currently running program.
 */
const char *xgetprogname(void);

/**
 * Wrapper for strtol() that forbids leading spaces.
 */
int xstrtol(const char *str, char **end, int base, long *value);

/**
 * Wrapper for strtoll() that forbids leading spaces.
 */
int xstrtoll(const char *str, char **end, int base, long long *value);

/**
 * Wrapper for strtof() that forbids leading spaces.
 */
int xstrtof(const char *str, char **end, float *value);

/**
 * Wrapper for strtod() that forbids leading spaces.
 */
int xstrtod(const char *str, char **end, double *value);

/**
 * Process a yes/no prompt.
 *
 * @return 1 for yes, 0 for no, and -1 for unknown.
 */
int ynprompt(void);

// #include <string.h>

/**
 * Get the length of the pure-ASCII prefix of a string.
 */
size_t asciilen(const char *str);

/**
 * Get the length of the pure-ASCII prefix of a string.
 *
 * @str
 *         The string to check.
 * @n
 *         The maximum prefix length.
 */
size_t asciinlen(const char *str, size_t n);

/**
 * Allocate a copy of a region of memory.
 *
 * @src
 *         The memory region to copy.
 * @size
 *         The size of the memory region.
 * @return
 *         A copy of the region, allocated with malloc(), or NULL on failure.
 */
void *xmemdup(const void *src, size_t size);

/**
 * A nice string copying function.
 *
 * @dest
 *         The NUL terminator of the destination string, or `end` if it is
 *         already truncated.
 * @end
 *         The end of the destination buffer.
 * @src
 *         The string to copy from.
 * @return
 *         The new NUL terminator of the destination, or `end` on truncation.
 */
char *xstpecpy(char *dest, char *end, const char *src);

/**
 * A nice string copying function.
 *
 * @dest
 *         The NUL terminator of the destination string, or `end` if it is
 *         already truncated.
 * @end
 *         The end of the destination buffer.
 * @src
 *         The string to copy from.
 * @n
 *         The maximum number of characters to copy.
 * @return
 *         The new NUL terminator of the destination, or `end` on truncation.
 */
char *xstpencpy(char *dest, char *end, const char *src, size_t n);

/**
 * Thread-safe strerror().
 *
 * @errnum
 *         An error number.
 * @return
 *         A string describing that error, which remains valid until the next
 *         xstrerror() call in the same thread.
 */
const char *xstrerror(int errnum);

/**
 * Shorthand for xstrerror(errno).
 */
const char *errstr(void);

/**
 * Format a mode like ls -l (e.g. -rw-r--r--).
 *
 * @mode
 *         The mode to format.
 * @str
 *         The string to hold the formatted mode.
 */
void xstrmode(mode_t mode, char str[11]);

#include <sys/resource.h>

/**
 * Compare two rlim_t values, accounting for infinite limits.
 */
int rlim_cmp(rlim_t a, rlim_t b);

#include <sys/types.h>

/**
 * Portable version of makedev().
 */
dev_t xmakedev(int ma, int mi);

/**
 * Portable version of major().
 */
int xmajor(dev_t dev);

/**
 * Portable version of minor().
 */
int xminor(dev_t dev);

// #include <sys/stat.h>

/**
 * Get the access/change/modification time from a struct stat.
 */
#if BFS_HAS_ST_ACMTIM
#  define ST_ATIM(sb) (sb).st_atim
#  define ST_CTIM(sb) (sb).st_ctim
#  define ST_MTIM(sb) (sb).st_mtim
#elif BFS_HAS_ST_ACMTIMESPEC
#  define ST_ATIM(sb) (sb).st_atimespec
#  define ST_CTIM(sb) (sb).st_ctimespec
#  define ST_MTIM(sb) (sb).st_mtimespec
#else
#  define ST_ATIM(sb) ((struct timespec) { .tv_sec = (sb).st_atime })
#  define ST_CTIM(sb) ((struct timespec) { .tv_sec = (sb).st_ctime })
#  define ST_MTIM(sb) ((struct timespec) { .tv_sec = (sb).st_mtime })
#endif

// #include <sys/wait.h>

/**
 * waitpid() wrapper that handles EINTR.
 */
pid_t xwaitpid(pid_t pid, int *status, int flags);

#include <sys/ioctl.h> // May be necessary for struct winsize
#include <termios.h>

/**
 * Open the controlling terminal.
 *
 * @flags
 *         The open() flags.
 * @return
 *         An open file descriptor, or -1 on failure.
 */
int open_cterm(int flags);

/**
 * tcgetwinsize()/ioctl(TIOCGWINSZ) wrapper.
 */
int xtcgetwinsize(int fd, struct winsize *ws);

// #include <unistd.h>

/**
 * Like dup(), but set the FD_CLOEXEC flag.
 *
 * @fd
 *         The file descriptor to duplicate.
 * @return
 *         A duplicated file descriptor, or -1 on failure.
 */
int dup_cloexec(int fd);

/**
 * Like pipe(), but set the FD_CLOEXEC flag.
 *
 * @pipefd
 *         The array to hold the two file descriptors.
 * @return
 *         0 on success, -1 on failure.
 */
int pipe_cloexec(int pipefd[2]);

/**
 * A safe version of read() that handles interrupted system calls and partial
 * reads.
 *
 * @return
 *         The number of bytes read.  A value != nbytes indicates an error
 *         (errno != 0) or end of file (errno == 0).
 */
size_t xread(int fd, void *buf, size_t nbytes);

/**
 * A safe version of write() that handles interrupted system calls and partial
 * writes.
 *
 * @return
 *         The number of bytes written.  A value != nbytes indicates an error.
 */
size_t xwrite(int fd, const void *buf, size_t nbytes);

/**
 * close() variant that preserves errno.
 *
 * @fd
 *         The file descriptor to close.
 */
void close_quietly(int fd);

/**
 * close() wrapper that asserts the file descriptor is valid.
 *
 * @fd
 *         The file descriptor to close.
 * @return
 *         0 on success, or -1 on error.
 */
int xclose(int fd);

/**
 * Wrapper for faccessat() that handles some portability issues.
 */
int xfaccessat(int fd, const char *path, int amode);

/**
 * readlinkat() wrapper that dynamically allocates the result.
 *
 * @fd
 *         The base directory descriptor.
 * @path
 *         The path to the link, relative to fd.
 * @size
 *         An estimate for the size of the link name (pass 0 if unknown).
 * @return
 *         The target of the link, allocated with malloc(), or NULL on failure.
 */
char *xreadlinkat(int fd, const char *path, size_t size);

/**
 * Wrapper for confstr() that allocates with malloc().
 *
 * @name
 *         The ID of the confstr to look up.
 * @return
 *         The value of the confstr, or NULL on failure.
 */
char *xconfstr(int name);

/**
 * Portability wrapper for strtofflags().
 *
 * @str
 *         The string to parse.  The pointee will be advanced to the first
 *         invalid position on error.
 * @set
 *         The flags that are set in the string.
 * @clear
 *         The flags that are cleared in the string.
 * @return
 *         0 on success, -1 on failure.
 */
int xstrtofflags(const char **str, unsigned long long *set, unsigned long long *clear);

/**
 * Wrapper for sysconf() that works around an MSan bug.
 */
long xsysconf(int name);

/**
 * Check for a POSIX option[1] at runtime.
 *
 * [1]: https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/V1_chap02.html#tag_02_01_06
 *
 * @name
 *         The symbolic name of the POSIX option (e.g. SPAWN).
 * @return
 *         The value of the option, either -1 or a date like 202405.
 */
#define sysoption(name) \
	(_POSIX_##name == 0 ? xsysconf(_SC_##name) : _POSIX_##name)

/**
 * Get the number of CPU threads available to the current process.
 */
long nproc(void);

#include <wchar.h>

/**
 * Error-recovering mbrtowc() wrapper.
 *
 * @str
 *         The string to convert.
 * @i
 *         The current index.
 * @len
 *         The length of the string.
 * @mb
 *         The multi-byte decoding state.
 * @return
 *         The wide character at index *i, or WEOF if decoding fails.  In either
 *         case, *i will be advanced to the next multi-byte character.
 */
wint_t xmbrtowc(const char *str, size_t *i, size_t len, mbstate_t *mb);

/**
 * wcswidth() variant that works on narrow strings.
 *
 * @str
 *         The string to measure.
 * @return
 *         The likely width of that string in a terminal.
 */
size_t xstrwidth(const char *str);

/**
 * wcwidth() wrapper that works around LLVM bug #65532.
 */
#define xwcwidth BFS_INTERCEPT(wcwidth)

#include <wctype.h>

/**
 * Wrap iswalpha()/iswdigit()/etc.
 */
#define BFS_ISWCTYPE(fn, c) BFS_INTERCEPT(fn)(c)

#define xiswalnum(c) BFS_ISWCTYPE(iswalnum, c)
#define xiswalpha(c) BFS_ISWCTYPE(iswalpha, c)
#define xiswcntrl(c) BFS_ISWCTYPE(iswcntrl, c)
#define xiswdigit(c) BFS_ISWCTYPE(iswdigit, c)
#define xiswlower(c) BFS_ISWCTYPE(iswlower, c)
#define xiswgraph(c) BFS_ISWCTYPE(iswgraph, c)
#define xiswprint(c) BFS_ISWCTYPE(iswprint, c)
#define xiswpunct(c) BFS_ISWCTYPE(iswpunct, c)
#define xiswspace(c) BFS_ISWCTYPE(iswspace, c)
#define xiswupper(c) BFS_ISWCTYPE(iswupper, c)
#define xiswxdigit(c) BFS_ISWCTYPE(iswxdigit, c)

// #include <wordexp.h>

/**
 * Flags for wordesc().
 */
enum wesc_flags {
	/**
	 * Escape special characters so that the shell will treat the escaped
	 * string as a single word.
	 */
	WESC_SHELL = 1 << 0,
	/**
	 * Escape special characters so that the escaped string is safe to print
	 * to a TTY.
	 */
	WESC_TTY = 1 << 1,
};

/**
 * Escape a string as a single shell word.
 *
 * @dest
 *         The destination string to fill.
 * @end
 *         The end of the destination buffer.
 * @src
 *         The string to escape.
 * @flags
 *         Controls which characters to escape.
 * @return
 *         The new NUL terminator of the destination, or `end` on truncation.
 */
char *wordesc(char *dest, char *end, const char *str, enum wesc_flags flags);

/**
 * Escape a string as a single shell word.
 *
 * @dest
 *         The destination string to fill.
 * @end
 *         The end of the destination buffer.
 * @src
 *         The string to escape.
 * @n
 *         The maximum length of the string.
 * @flags
 *         Controls which characters to escape.
 * @return
 *         The new NUL terminator of the destination, or `end` on truncation.
 */
char *wordnesc(char *dest, char *end, const char *str, size_t n, enum wesc_flags flags);

#endif // BFS_BFSTD_H
