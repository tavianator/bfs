// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include "prelude.h"
#include "bfstd.h"
#include "bit.h"
#include "diag.h"
#include "sanity.h"
#include "thread.h"
#include "xregex.h"
#include <errno.h>
#include <fcntl.h>
#include <langinfo.h>
#include <limits.h>
#include <locale.h>
#include <nl_types.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wchar.h>

#if BFS_USE_SYS_SYSMACROS_H
#  include <sys/sysmacros.h>
#elif BFS_USE_SYS_MKDEV_H
#  include <sys/mkdev.h>
#endif

#if BFS_USE_UTIL_H
#  include <util.h>
#endif

bool error_is_like(int error, int category) {
	if (error == category) {
		return true;
	}

	switch (category) {
	case ENOENT:
		return error == ENOTDIR;

	case ENOSYS:
		// https://github.com/opencontainers/runc/issues/2151
		return errno == EPERM;

#if __DragonFly__
	// https://twitter.com/tavianator/status/1742991411203485713
	case ENAMETOOLONG:
		return error == EFAULT;
#endif
	}

	return false;
}

bool errno_is_like(int category) {
	return error_is_like(errno, category);
}

int try(int ret) {
	if (ret >= 0) {
		return ret;
	} else {
		bfs_assert(errno > 0, "errno should be positive, was %d\n", errno);
		return -errno;
	}
}

char *xdirname(const char *path) {
	size_t i = xbaseoff(path);

	// Skip trailing slashes
	while (i > 0 && path[i - 1] == '/') {
		--i;
	}

	if (i > 0) {
		return strndup(path, i);
	} else if (path[i] == '/') {
		return strdup("/");
	} else {
		return strdup(".");
	}
}

char *xbasename(const char *path) {
	size_t i = xbaseoff(path);
	size_t len = strcspn(path + i, "/");
	if (len > 0) {
		return strndup(path + i, len);
	} else if (path[i] == '/') {
		return strdup("/");
	} else {
		return strdup(".");
	}
}

size_t xbaseoff(const char *path) {
	size_t i = strlen(path);

	// Skip trailing slashes
	while (i > 0 && path[i - 1] == '/') {
		--i;
	}

	// Find the beginning of the name
	while (i > 0 && path[i - 1] != '/') {
		--i;
	}

	// Skip leading slashes
	while (path[i] == '/' && path[i + 1]) {
		++i;
	}

	return i;
}

FILE *xfopen(const char *path, int flags) {
	char mode[4];

	switch (flags & O_ACCMODE) {
	case O_RDONLY:
		strcpy(mode, "rb");
		break;
	case O_WRONLY:
		strcpy(mode, "wb");
		break;
	case O_RDWR:
		strcpy(mode, "r+b");
		break;
	default:
		bfs_bug("Invalid access mode");
		errno = EINVAL;
		return NULL;
	}

	if (flags & O_APPEND) {
		mode[0] = 'a';
	}

	int fd;
	if (flags & O_CREAT) {
		fd = open(path, flags, 0666);
	} else {
		fd = open(path, flags);
	}

	if (fd < 0) {
		return NULL;
	}

	FILE *ret = fdopen(fd, mode);
	if (!ret) {
		close_quietly(fd);
		return NULL;
	}

	return ret;
}

char *xgetdelim(FILE *file, char delim) {
	char *chunk = NULL;
	size_t n = 0;
	ssize_t len = getdelim(&chunk, &n, delim, file);
	if (len >= 0) {
		if (chunk[len] == delim) {
			chunk[len] = '\0';
		}
		return chunk;
	} else {
		free(chunk);
		if (!ferror(file)) {
			errno = 0;
		}
		return NULL;
	}
}

int open_cterm(int flags) {
	char path[L_ctermid];
	if (ctermid(path) == NULL || strlen(path) == 0) {
		errno = ENOTTY;
		return -1;
	}

	return open(path, flags);
}

const char *xgetprogname(void) {
	const char *cmd = NULL;
#if BFS_HAS_GETPROGNAME
	cmd = getprogname();
#elif BFS_HAS_GETPROGNAME_GNU
	cmd = program_invocation_short_name;
#endif

	if (!cmd) {
		cmd = BFS_COMMAND;
	}

	return cmd;
}

int xstrtoll(const char *str, char **end, int base, long long *value) {
	// strtoll() skips leading spaces, but we want to reject them
	if (xisspace(str[0])) {
		errno = EINVAL;
		return -1;
	}

	// If end is NULL, make sure the entire string is valid
	bool entire = !end;
	char *endp;
	if (!end) {
		end = &endp;
	}

	errno = 0;
	long long result = strtoll(str, end, base);
	if (errno != 0) {
		return -1;
	}

	if (*end == str || (entire && **end != '\0')) {
		errno = EINVAL;
		return -1;
	}

	*value = result;
	return 0;
}

/** Compile and execute a regular expression for xrpmatch(). */
static int xrpregex(nl_item item, const char *response) {
	const char *pattern = nl_langinfo(item);
	if (!pattern) {
		return -1;
	}

	struct bfs_regex *regex;
	int ret = bfs_regcomp(&regex, pattern, BFS_REGEX_POSIX_EXTENDED, 0);
	if (ret == 0) {
		ret = bfs_regexec(regex, response, 0);
	}

	bfs_regfree(regex);
	return ret;
}

/** Check if a response is affirmative or negative. */
static int xrpmatch(const char *response) {
	int ret = xrpregex(NOEXPR, response);
	if (ret > 0) {
		return 0;
	} else if (ret < 0) {
		return -1;
	}

	ret = xrpregex(YESEXPR, response);
	if (ret > 0) {
		return 1;
	} else if (ret < 0) {
		return -1;
	}

	// Failsafe: always handle y/n
	char c = response[0];
	if (c == 'n' || c == 'N') {
		return 0;
	} else if (c == 'y' || c == 'Y') {
		return 1;
	} else {
		return -1;
	}
}

int ynprompt(void) {
	fflush(stderr);

	char *line = xgetdelim(stdin, '\n');
	int ret = line ? xrpmatch(line) : -1;
	free(line);
	return ret;
}

void *xmemdup(const void *src, size_t size) {
	void *ret = malloc(size);
	if (ret) {
		memcpy(ret, src, size);
	}
	return ret;
}

char *xstpecpy(char *dest, char *end, const char *src) {
	return xstpencpy(dest, end, src, SIZE_MAX);
}

char *xstpencpy(char *dest, char *end, const char *src, size_t n) {
	size_t space = end - dest;
	n = space < n ? space : n;
	n = strnlen(src, n);
	memcpy(dest, src, n);
	if (n < space) {
		dest[n] = '\0';
		return dest + n;
	} else {
		end[-1] = '\0';
		return end;
	}
}

const char *xstrerror(int errnum) {
	int saved = errno;
	const char *ret = NULL;
	static thread_local char buf[256];

	// On FreeBSD with MemorySanitizer, duplocale() triggers
	// https://github.com/llvm/llvm-project/issues/65532
#if BFS_HAS_STRERROR_L && !(__FreeBSD__ && SANITIZE_MEMORY)
#  if BFS_HAS_USELOCALE
	locale_t loc = uselocale((locale_t)0);
#  else
	locale_t loc = LC_GLOBAL_LOCALE;
#  endif

	bool free_loc = false;
	if (loc == LC_GLOBAL_LOCALE) {
		loc = duplocale(loc);
		free_loc = true;
	}

	if (loc != (locale_t)0) {
		ret = strerror_l(errnum, loc);
		if (free_loc) {
			freelocale(loc);
		}
	}
#elif BFS_HAS_STRERROR_R_POSIX
	if (strerror_r(errnum, buf, sizeof(buf)) == 0) {
		ret = buf;
	}
#elif BFS_HAS_STRERROR_R_GNU
	ret = strerror_r(errnum, buf, sizeof(buf));
#endif

	if (!ret) {
		// Fallback for strerror_[lr]() or duplocale() failures
		snprintf(buf, sizeof(buf), "Unknown error %d", errnum);
		ret = buf;
	}

	errno = saved;
	return ret;
}

const char *errstr(void) {
	return xstrerror(errno);
}

/** Get the single character describing the given file type. */
static char type_char(mode_t mode) {
	switch (mode & S_IFMT) {
	case S_IFREG:
		return '-';
	case S_IFBLK:
		return 'b';
	case S_IFCHR:
		return 'c';
	case S_IFDIR:
		return 'd';
	case S_IFLNK:
		return 'l';
	case S_IFIFO:
		return 'p';
	case S_IFSOCK:
		return 's';
#ifdef S_IFDOOR
	case S_IFDOOR:
		return 'D';
#endif
#ifdef S_IFPORT
	case S_IFPORT:
		return 'P';
#endif
#ifdef S_IFWHT
	case S_IFWHT:
		return 'w';
#endif
	}

	return '?';
}

void xstrmode(mode_t mode, char str[11]) {
	strcpy(str, "----------");

	str[0] = type_char(mode);

	if (mode & 00400) {
		str[1] = 'r';
	}
	if (mode & 00200) {
		str[2] = 'w';
	}
	if ((mode & 04100) == 04000) {
		str[3] = 'S';
	} else if (mode & 04000) {
		str[3] = 's';
	} else if (mode & 00100) {
		str[3] = 'x';
	}

	if (mode & 00040) {
		str[4] = 'r';
	}
	if (mode & 00020) {
		str[5] = 'w';
	}
	if ((mode & 02010) == 02000) {
		str[6] = 'S';
	} else if (mode & 02000) {
		str[6] = 's';
	} else if (mode & 00010) {
		str[6] = 'x';
	}

	if (mode & 00004) {
		str[7] = 'r';
	}
	if (mode & 00002) {
		str[8] = 'w';
	}
	if ((mode & 01001) == 01000) {
		str[9] = 'T';
	} else if (mode & 01000) {
		str[9] = 't';
	} else if (mode & 00001) {
		str[9] = 'x';
	}
}

/** Check if an rlimit value is infinite. */
static bool rlim_isinf(rlim_t r) {
	// Consider RLIM_{INFINITY,SAVED_{CUR,MAX}} all equally infinite
	if (r == RLIM_INFINITY) {
		return true;
	}

#ifdef RLIM_SAVED_CUR
	if (r == RLIM_SAVED_CUR) {
		return true;
	}
#endif

#ifdef RLIM_SAVED_MAX
	if (r == RLIM_SAVED_MAX) {
		return true;
	}
#endif

	return false;
}

int rlim_cmp(rlim_t a, rlim_t b) {
	bool a_inf = rlim_isinf(a);
	bool b_inf = rlim_isinf(b);
	if (a_inf || b_inf) {
		return a_inf - b_inf;
	}

	return (a > b) - (a < b);
}

dev_t xmakedev(int ma, int mi) {
#ifdef makedev
	return makedev(ma, mi);
#else
	return (ma << 8) | mi;
#endif
}

int xmajor(dev_t dev) {
#ifdef major
	return major(dev);
#else
	return dev >> 8;
#endif
}

int xminor(dev_t dev) {
#ifdef minor
	return minor(dev);
#else
	return dev & 0xFF;
#endif
}

pid_t xwaitpid(pid_t pid, int *status, int flags) {
	pid_t ret;
	do {
		ret = waitpid(pid, status, flags);
	} while (ret < 0 && errno == EINTR);
	return ret;
}

int dup_cloexec(int fd) {
#ifdef F_DUPFD_CLOEXEC
	return fcntl(fd, F_DUPFD_CLOEXEC, 0);
#else
	int ret = dup(fd);
	if (ret < 0) {
		return -1;
	}

	if (fcntl(ret, F_SETFD, FD_CLOEXEC) == -1) {
		close_quietly(ret);
		return -1;
	}

	return ret;
#endif
}

int pipe_cloexec(int pipefd[2]) {
#if BFS_HAS_PIPE2
	return pipe2(pipefd, O_CLOEXEC);
#else
	if (pipe(pipefd) != 0) {
		return -1;
	}

	if (fcntl(pipefd[0], F_SETFD, FD_CLOEXEC) == -1 || fcntl(pipefd[1], F_SETFD, FD_CLOEXEC) == -1) {
		close_quietly(pipefd[1]);
		close_quietly(pipefd[0]);
		return -1;
	}

	return 0;
#endif
}

size_t xread(int fd, void *buf, size_t nbytes) {
	size_t count = 0;

	while (count < nbytes) {
		ssize_t ret = read(fd, (char *)buf + count, nbytes - count);
		if (ret < 0) {
			if (errno == EINTR) {
				continue;
			} else {
				break;
			}
		} else if (ret == 0) {
			// EOF
			errno = 0;
			break;
		} else {
			count += ret;
		}
	}

	return count;
}

size_t xwrite(int fd, const void *buf, size_t nbytes) {
	size_t count = 0;

	while (count < nbytes) {
		ssize_t ret = write(fd, (const char *)buf + count, nbytes - count);
		if (ret < 0) {
			if (errno == EINTR) {
				continue;
			} else {
				break;
			}
		} else if (ret == 0) {
			// EOF?
			errno = 0;
			break;
		} else {
			count += ret;
		}
	}

	return count;
}

void close_quietly(int fd) {
	int error = errno;
	xclose(fd);
	errno = error;
}

int xclose(int fd) {
	int ret = close(fd);
	if (ret != 0) {
		bfs_verify(errno != EBADF);
	}
	return ret;
}

int xfaccessat(int fd, const char *path, int amode) {
	int ret = faccessat(fd, path, amode, 0);

#ifdef AT_EACCESS
	// Some platforms, like Hurd, only support AT_EACCESS.  Other platforms,
	// like Android, don't support AT_EACCESS at all.
	if (ret != 0 && (errno == EINVAL || errno == ENOTSUP)) {
		ret = faccessat(fd, path, amode, AT_EACCESS);
	}
#endif

	return ret;
}

char *xconfstr(int name) {
#if BFS_HAS_CONFSTR
	size_t len = confstr(name, NULL, 0);
	if (len == 0) {
		return NULL;
	}

	char *str = malloc(len);
	if (!str) {
		return NULL;
	}

	if (confstr(name, str, len) != len) {
		free(str);
		return NULL;
	}

	return str;
#else
	errno = ENOTSUP;
	return NULL;
#endif
}

char *xreadlinkat(int fd, const char *path, size_t size) {
	ssize_t len;
	char *name = NULL;

	if (size == 0) {
		size = 64;
	} else {
		++size; // NUL terminator
	}

	while (true) {
		char *new_name = realloc(name, size);
		if (!new_name) {
			goto error;
		}
		name = new_name;

		len = readlinkat(fd, path, name, size);
		if (len < 0) {
			goto error;
		} else if ((size_t)len >= size) {
			size *= 2;
		} else {
			break;
		}
	}

	name[len] = '\0';
	return name;

error:
	free(name);
	return NULL;
}

#if BFS_HAS_STRTOFFLAGS
#  define BFS_STRTOFFLAGS strtofflags
#elif BFS_HAS_STRING_TO_FLAGS
#  define BFS_STRTOFFLAGS string_to_flags
#endif

int xstrtofflags(const char **str, unsigned long long *set, unsigned long long *clear) {
#ifdef BFS_STRTOFFLAGS
	char *str_arg = (char *)*str;

#if __OpenBSD__
	typedef uint32_t bfs_fflags_t;
#else
	typedef unsigned long bfs_fflags_t;
#endif
	bfs_fflags_t set_arg = 0;
	bfs_fflags_t clear_arg = 0;

	int ret = BFS_STRTOFFLAGS(&str_arg, &set_arg, &clear_arg);

	*str = str_arg;
	*set = set_arg;
	*clear = clear_arg;

	if (ret != 0) {
		errno = EINVAL;
	}
	return ret;
#else // !BFS_STRTOFFLAGS
	errno = ENOTSUP;
	return -1;
#endif
}

long xsysconf(int name) {
#if __FreeBSD__ && SANITIZE_MEMORY
	// Work around https://github.com/llvm/llvm-project/issues/88163
	__msan_scoped_disable_interceptor_checks();
#endif

	long ret = sysconf(name);

#if __FreeBSD__ && SANITIZE_MEMORY
	__msan_scoped_enable_interceptor_checks();
#endif

	return ret;
}

size_t asciilen(const char *str) {
	return asciinlen(str, strlen(str));
}

size_t asciinlen(const char *str, size_t n) {
	size_t i = 0;

#if SIZE_WIDTH % 8 == 0
	// Word-at-a-time isascii()
	for (size_t word; i + sizeof(word) <= n; i += sizeof(word)) {
		memcpy(&word, str + i, sizeof(word));

		const size_t mask = (SIZE_MAX / 0xFF) << 7; // 0x808080...
		word &= mask;
		if (!word) {
			continue;
		}

#if ENDIAN_NATIVE == ENDIAN_BIG
		word = bswap(word);
#elif ENDIAN_NATIVE != ENDIAN_LITTLE
		break;
#endif

		size_t first = trailing_zeros(word) / 8;
		return i + first;
	}
#endif

	for (; i < n; ++i) {
		if (!xisascii(str[i])) {
			break;
		}
	}

	return i;
}

wint_t xmbrtowc(const char *str, size_t *i, size_t len, mbstate_t *mb) {
	wchar_t wc;
	size_t mblen = mbrtowc(&wc, str + *i, len - *i, mb);
	switch (mblen) {
	case -1: // Invalid byte sequence
	case -2: // Incomplete byte sequence
		*i += 1;
		*mb = (mbstate_t){0};
		return WEOF;
	default:
		*i += mblen;
		return wc;
	}
}

size_t xstrwidth(const char *str) {
	size_t len = strlen(str);
	size_t ret = 0;

	size_t asclen = asciinlen(str, len);
	size_t i;
	for (i = 0; i < asclen; ++i) {
		// Assume all ASCII printables have width 1
		if (xisprint(str[i])) {
			++ret;
		}
	}

	mbstate_t mb = {0};
	while (i < len) {
		wint_t wc = xmbrtowc(str, &i, len, &mb);
		if (wc == WEOF) {
			// Assume a single-width '?'
			++ret;
			continue;
		}

		int width = xwcwidth(wc);
		if (width > 0) {
			ret += width;
		}
	}

	return ret;
}

/**
 * Character type flags.
 */
enum ctype {
	IS_PRINT = 1 << 0,
	IS_SPACE = 1 << 1,
};

/** Cached ctypes. */
static unsigned char ctype_cache[UCHAR_MAX + 1];

/** Initialize the ctype cache. */
static void char_cache_init(void) {
	for (size_t c = 0; c <= UCHAR_MAX; ++c) {
		if (xisprint(c)) {
			ctype_cache[c] |= IS_PRINT;
		}
		if (xisspace(c)) {
			ctype_cache[c] |= IS_SPACE;
		}
	}
}

/** Check if a character is printable. */
static bool wesc_isprint(unsigned char c, enum wesc_flags flags) {
	if (ctype_cache[c] & IS_PRINT) {
		return true;
	}

	// Technically a literal newline is safe inside single quotes, but $'\n'
	// is much nicer than '
	// '
	if (!(flags & WESC_SHELL) && (ctype_cache[c] & IS_SPACE)) {
		return true;
	}

	return false;
}

/** Check if a wide character is printable. */
static bool wesc_iswprint(wchar_t c, enum wesc_flags flags) {
	if (xiswprint(c)) {
		return true;
	}

	if (!(flags & WESC_SHELL) && xiswspace(c)) {
		return true;
	}

	return false;
}

/** Get the length of the longest printable prefix of a string. */
static size_t printable_len(const char *str, size_t len, enum wesc_flags flags) {
	static pthread_once_t once = PTHREAD_ONCE_INIT;
	invoke_once(&once, char_cache_init);

	// Fast path: avoid multibyte checks
	size_t asclen = asciinlen(str, len);
	size_t i;
	for (i = 0; i < asclen; ++i) {
		if (!wesc_isprint(str[i], flags)) {
			return i;
		}
	}

	mbstate_t mb = {0};
	for (size_t j = i; i < len; i = j) {
		wint_t wc = xmbrtowc(str, &j, len, &mb);
		if (wc == WEOF) {
			break;
		}
		if (!wesc_iswprint(wc, flags)) {
			break;
		}
	}

	return i;
}

/** Convert a special char into a well-known escape sequence like "\n". */
static const char *dollar_esc(char c) {
	// https://www.gnu.org/software/bash/manual/html_node/ANSI_002dC-Quoting.html
	switch (c) {
	case '\a':
		return "\\a";
	case '\b':
		return "\\b";
	case '\033':
		return "\\e";
	case '\f':
		return "\\f";
	case '\n':
		return "\\n";
	case '\r':
		return "\\r";
	case '\t':
		return "\\t";
	case '\v':
		return "\\v";
	case '\'':
		return "\\'";
	case '\\':
		return "\\\\";
	default:
		return NULL;
	}
}

/** $'Quote' a string for the shell. */
static char *dollar_quote(char *dest, char *end, const char *str, size_t len, enum wesc_flags flags) {
	dest = xstpecpy(dest, end, "$'");

	mbstate_t mb = {0};
	for (size_t i = 0; i < len;) {
		size_t start = i;
		bool safe = false;

		wint_t wc = xmbrtowc(str, &i, len, &mb);
		if (wc != WEOF) {
			safe = wesc_iswprint(wc, flags);
		}

		for (size_t j = start; safe && j < i; ++j) {
			if (str[j] == '\'' || str[j] == '\\') {
				safe = false;
			}
		}

		if (safe) {
			dest = xstpencpy(dest, end, str + start, i - start);
		} else {
			for (size_t j = start; j < i; ++j) {
				unsigned char byte = str[j];
				const char *esc = dollar_esc(byte);
				if (esc) {
					dest = xstpecpy(dest, end, esc);
				} else {
					static const char *hex[] = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "A", "B", "C", "D", "E", "F"};
					dest = xstpecpy(dest, end, "\\x");
					dest = xstpecpy(dest, end, hex[byte / 0x10]);
					dest = xstpecpy(dest, end, hex[byte % 0x10]);
				}
			}
		}
	}

	return xstpecpy(dest, end, "'");
}

/** How much of this string is safe as a bare word? */
static size_t bare_len(const char *str, size_t len) {
	// https://pubs.opengroup.org/onlinepubs/9799919799/utilities/V3_chap02.html#tag_19_02
	size_t ret = strcspn(str, "|&;<>()$`\\\"' *?[#~=%!{}");
	return ret < len ? ret : len;
}

/** How much of this string is safe to double-quote? */
static size_t quotable_len(const char *str, size_t len) {
	// https://pubs.opengroup.org/onlinepubs/9799919799/utilities/V3_chap02.html#tag_19_02_03
	size_t ret = strcspn(str, "`$\\\"!");
	return ret < len ? ret : len;
}

/** "Quote" a string for the shell. */
static char *double_quote(char *dest, char *end, const char *str, size_t len) {
	dest = xstpecpy(dest, end, "\"");
	dest = xstpencpy(dest, end, str, len);
	return xstpecpy(dest, end, "\"");
}

/** 'Quote' a string for the shell. */
static char *single_quote(char *dest, char *end, const char *str, size_t len) {
	bool open = false;

	while (len > 0) {
		size_t chunk = strcspn(str, "'");
		chunk = chunk < len ? chunk : len;
		if (chunk > 0) {
			if (!open) {
				dest = xstpecpy(dest, end, "'");
				open = true;
			}
			dest = xstpencpy(dest, end, str, chunk);
			str += chunk;
			len -= chunk;
		}

		while (len > 0 && *str == '\'') {
			if (open) {
				dest = xstpecpy(dest, end, "'");
				open = false;
			}
			dest = xstpecpy(dest, end, "\\'");
			++str;
			--len;
		}
	}

	if (open) {
		dest = xstpecpy(dest, end, "'");
	}

	return dest;
}

char *wordesc(char *dest, char *end, const char *str, enum wesc_flags flags) {
	return wordnesc(dest, end, str, SIZE_MAX, flags);
}

char *wordnesc(char *dest, char *end, const char *str, size_t n, enum wesc_flags flags) {
	size_t len = strnlen(str, n);
	char *start = dest;

	if (printable_len(str, len, flags) < len) {
		// String contains unprintable chars, use $'this\x7Fsyntax'
		dest = dollar_quote(dest, end, str, len, flags);
	} else if (!(flags & WESC_SHELL) || bare_len(str, len) == len) {
		// Whole string is safe as a bare word
		dest = xstpencpy(dest, end, str, len);
	} else if (quotable_len(str, len) == len) {
		// Whole string is safe to double-quote
		dest = double_quote(dest, end, str, len);
	} else {
		// Single-quote the whole string
		dest = single_quote(dest, end, str, len);
	}

	if (dest == start) {
		dest = xstpecpy(dest, end, "\"\"");
	}

	return dest;
}
