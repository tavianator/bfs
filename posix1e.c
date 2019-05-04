/****************************************************************************
 * bfs                                                                      *
 * Copyright (C) 2019 Tavian Barnes <tavianator@tavianator.com>             *
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

#include "posix1e.h"
#include "bftw.h"
#include "util.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if BFS_HAS_SYS_ACL
#	include <sys/acl.h>
#endif

#if BFS_HAS_POSIX1E_CAPABILITIES
#	include <sys/capability.h>
#endif

#if BFS_HAS_SYS_ACL || BFS_HAS_POSIX1E_CAPABILITIES

static const char *open_path(const struct BFTW *ftwbuf, int *fd) {
#ifdef O_PATH
	// The POSIX.1e APIS predate the *at() family of functions.  We'd still
	// like to do something to avoid path re-traversals and limit races
	// though.  Ideally we could just do openat(..., O_PATH) (since we may
	// not have read access) and pass that fd to something like cap_get_fd()
	// but that will fail since fgetxattr() needs read access to the file.
	// The workaround is to use O_PATH to open an fd and then pass
	// /proc/self/fd/<fd> to cap_get_path().  Inspired by
	// https://android.googlesource.com/platform/bionic/+/2825f10b7f61558c264231a536cf3affc0d84204
	int flags = O_PATH;
	if (ftwbuf->stat_flags & BFS_STAT_NOFOLLOW) {
		flags |= O_NOFOLLOW;
	}

	*fd = openat(ftwbuf->at_fd, ftwbuf->at_path, flags);
	if (*fd < 0) {
		return NULL;
	}

	size_t size = strlen("/proc/self/fd/") + CHAR_BIT*sizeof(int) + 1;
	char *path = malloc(size);
	if (!path) {
		close(*fd);
		*fd = -1;
		return NULL;
	}

	snprintf(path, size, "/proc/self/fd/%d", *fd);
	return path;
#else
	*fd = -1;
	return ftwbuf->path;
#endif
}

static void close_path(const struct BFTW *ftwbuf, const char *path, int fd) {
	if (path && path != ftwbuf->path) {
		free((void *)path);
	}
	if (fd >= 0) {
		close(fd);
	}
}

#endif // BFS_HAS_SYS_ACL || BFS_HAS_POSIX1E_CAPABILITIES

#if BFS_HAS_SYS_ACL

/** Check if any ACLs of the given type are non-trivial. */
static bool bfs_check_acl_type(const char *path, acl_type_t type) {
	acl_t acl = acl_get_file(path, type);
	if (!acl) {
		return false;
	}

	bool ret = false;
	acl_entry_t entry;
	for (int status = acl_get_entry(acl, ACL_FIRST_ENTRY, &entry);
	     status > 0;
	     status = acl_get_entry(acl, ACL_NEXT_ENTRY, &entry)) {
#if defined(ACL_USER_OBJ) && defined(ACL_GROUP_OBJ) && defined(ACL_OTHER)
		acl_tag_t tag;
		if (acl_get_tag_type(entry, &tag) != 0) {
			continue;
		}
		if (tag != ACL_USER_OBJ && tag != ACL_GROUP_OBJ && tag != ACL_OTHER) {
			ret = true;
			break;
		}
#else
		ret = true;
		break;
#endif
	}

	acl_free(acl);
	return ret;
}

bool bfs_check_acl(const struct BFTW *ftwbuf) {
	if (ftwbuf->typeflag == BFTW_LNK) {
		return false;
	}

	int fd;
	const char *path = open_path(ftwbuf, &fd);
	if (!path) {
		return false;
	}

	bool ret = false;
	if (bfs_check_acl_type(path, ACL_TYPE_ACCESS)) {
		ret = true;
	} else if (bfs_check_acl_type(path, ACL_TYPE_DEFAULT)) {
		ret = true;
#ifdef ACL_TYPE_EXTENDED
	} else if (bfs_check_acl_type(path, ACL_TYPE_EXTENDED)) {
		ret = true;
#endif
	}

	close_path(ftwbuf, path, fd);
	return ret;
}

#else // !BFS_HAS_SYS_ACL

bool bfs_check_acl(const struct BFTW *ftwbuf) {
	return false;
}

#endif

#if BFS_HAS_POSIX1E_CAPABILITIES

bool bfs_check_capabilities(const struct BFTW *ftwbuf) {
	bool ret = false;

	if (ftwbuf->typeflag == BFTW_LNK) {
		goto out;
	}

	int fd;
	const char *path = open_path(ftwbuf, &fd);
	if (!path) {
		goto out;
	}

	cap_t caps = cap_get_file(path);
	if (!caps) {
		goto out_close;
	}

	// TODO: Any better way to check for a non-empty capability set?
	char *text = cap_to_text(caps, NULL);
	if (!text) {
		goto out_free_caps;
	}
	ret = text[0];

	cap_free(text);
out_free_caps:
	cap_free(caps);
out_close:
	close_path(ftwbuf, path, fd);
out:
	return ret;
}

#else // !BFS_HAS_POSIX1E_CAPABILITIES

bool bfs_check_capabilities(const struct BFTW *ftwbuf) {
	return false;
}

#endif
