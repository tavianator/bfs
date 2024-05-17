// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

#include "prelude.h"
#include "fsade.h"
#include "atomic.h"
#include "bfstd.h"
#include "bftw.h"
#include "dir.h"
#include "dstring.h"
#include "sanity.h"
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <unistd.h>

#if BFS_CAN_CHECK_ACL
#  include <sys/acl.h>
#endif

#if BFS_CAN_CHECK_CAPABILITIES
#  include <sys/capability.h>
#endif

#if BFS_CAN_CHECK_CONTEXT
#  include <selinux/selinux.h>
#endif

#if BFS_USE_SYS_EXTATTR_H
#  include <sys/extattr.h>
#elif BFS_USE_SYS_XATTR_H
#  include <sys/xattr.h>
#endif

/**
 * Many of the APIs used here don't have *at() variants, but we can try to
 * emulate something similar if /proc/self/fd is available.
 */
attr(maybe_unused)
static const char *fake_at(const struct BFTW *ftwbuf) {
	static atomic int proc_works = -1;

	dchar *path = NULL;
	if (ftwbuf->at_fd == (int)AT_FDCWD || load(&proc_works, relaxed) == 0) {
		goto fail;
	}

	path = dstrprintf("/proc/self/fd/%d/", ftwbuf->at_fd);
	if (!path) {
		goto fail;
	}

	if (load(&proc_works, relaxed) < 0) {
		if (xfaccessat(AT_FDCWD, path, F_OK) != 0) {
			store(&proc_works, 0, relaxed);
			goto fail;
		} else {
			store(&proc_works, 1, relaxed);
		}
	}

	if (dstrcat(&path, ftwbuf->at_path) != 0) {
		goto fail;
	}

	return path;

fail:
	dstrfree(path);
	return ftwbuf->path;
}

attr(maybe_unused)
static void free_fake_at(const struct BFTW *ftwbuf, const char *path) {
	if (path != ftwbuf->path) {
		dstrfree((dchar *)path);
	}
}

/**
 * Check if an error was caused by the absence of support or data for a feature.
 */
attr(maybe_unused)
static bool is_absence_error(int error) {
	// If the OS doesn't support the feature, it's obviously not enabled for
	// any files
	if (error == ENOTSUP) {
		return true;
	}

	// On Linux, ACLs and capabilities are implemented in terms of extended
	// attributes, which report ENODATA/ENOATTR when missing

#ifdef ENODATA
	if (error == ENODATA) {
		return true;
	}
#endif

#if defined(ENOATTR) && ENOATTR != ENODATA
	if (error == ENOATTR) {
		return true;
	}
#endif

	// On at least FreeBSD and macOS, EINVAL is returned when the requested
	// ACL type is not supported for that file
	if (error == EINVAL) {
		return true;
	}

#if __APPLE__
	// On macOS, ENOENT can also signal that a file has no ACLs
	if (error == ENOENT) {
		return true;
	}
#endif

	return false;
}

#if BFS_CAN_CHECK_ACL

#if BFS_HAS_ACL_GET_FILE

/** Unified interface for incompatible acl_get_entry() implementations. */
static int bfs_acl_entry(acl_t acl, int which, acl_entry_t *entry) {
#if BFS_HAS_ACL_GET_ENTRY
	int ret = acl_get_entry(acl, which, entry);
#  if __APPLE__
	// POSIX.1e specifies a return value of 1 for success, but macOS returns 0 instead
	return !ret;
#  else
	return ret;
#  endif
#elif __DragonFly__
#  if !defined(ACL_FIRST_ENTRY) && !defined(ACL_NEXT_ENTRY)
#    define ACL_FIRST_ENTRY 0
#    define ACL_NEXT_ENTRY  1
#  endif

	switch (which) {
	case ACL_FIRST_ENTRY:
		*entry = &acl->acl_entry[0];
		break;
	case ACL_NEXT_ENTRY:
		++*entry;
		break;
	default:
		errno = EINVAL;
		return -1;
	}

	acl_entry_t last = &acl->acl_entry[acl->acl_cnt];
	return *entry == last;
#else
	errno = ENOTSUP;
	return -1;
#endif
}

/** Unified interface for acl_get_tag_type(). */
attr(maybe_unused)
static int bfs_acl_tag_type(acl_entry_t entry, acl_tag_t *tag) {
#if BFS_HAS_ACL_GET_TAG_TYPE
	return acl_get_tag_type(entry, tag);
#elif __DragonFly__
	*tag = entry->ae_tag;
	return 0;
#else
	errno = ENOTSUP;
	return -1;
#endif
}

/** Check if a POSIX.1e ACL is non-trivial. */
static int bfs_check_posix1e_acl(acl_t acl, bool ignore_required) {
	int ret = 0;

	acl_entry_t entry;
	for (int status = bfs_acl_entry(acl, ACL_FIRST_ENTRY, &entry);
	     status > 0;
	     status = bfs_acl_entry(acl, ACL_NEXT_ENTRY, &entry))
	{
#if defined(ACL_USER_OBJ) && defined(ACL_GROUP_OBJ) && defined(ACL_OTHER)
		if (ignore_required) {
			acl_tag_t tag;
			if (bfs_acl_tag_type(entry, &tag) != 0) {
				ret = -1;
				continue;
			}
			if (tag == ACL_USER_OBJ || tag == ACL_GROUP_OBJ || tag == ACL_OTHER) {
				continue;
			}
		}
#endif

		ret = 1;
		break;
	}

	return ret;
}

/** Check if an ACL of the given type is non-trivial. */
static int bfs_check_acl_type(acl_t acl, acl_type_t type) {
	if (type == ACL_TYPE_DEFAULT) {
		// For directory default ACLs, any entries make them non-trivial
		return bfs_check_posix1e_acl(acl, false);
	}

#if BFS_HAS_ACL_IS_TRIVIAL_NP
	int trivial;
	int ret = acl_is_trivial_np(acl, &trivial);

	// msan seems to be missing an interceptor for acl_is_trivial_np()
	sanitize_init(&trivial);

	if (ret < 0) {
		return -1;
	} else if (trivial) {
		return 0;
	} else {
		return 1;
	}
#else
	return bfs_check_posix1e_acl(acl, true);
#endif
}

#endif // BFS_HAS_ACL_GET_FILE

int bfs_check_acl(const struct BFTW *ftwbuf) {
	if (ftwbuf->type == BFS_LNK) {
		return 0;
	}

	const char *path = fake_at(ftwbuf);

#if BFS_HAS_ACL_TRIVIAL
	int ret = acl_trivial(path);
	int error = errno;
#elif BFS_HAS_ACL_GET_FILE
	static const acl_type_t acl_types[] = {
#  if __APPLE__
		// macOS gives EINVAL for either of the two standard ACL types,
		// supporting only ACL_TYPE_EXTENDED
		ACL_TYPE_EXTENDED,
#  else
		// The two standard POSIX.1e ACL types
		ACL_TYPE_ACCESS,
		ACL_TYPE_DEFAULT,
#  endif

#  ifdef ACL_TYPE_NFS4
		ACL_TYPE_NFS4,
#  endif
	};

	int ret = -1, error = 0;
	for (size_t i = 0; i < countof(acl_types) && ret <= 0; ++i) {
		acl_type_t type = acl_types[i];

		if (type == ACL_TYPE_DEFAULT && ftwbuf->type != BFS_DIR) {
			// ACL_TYPE_DEFAULT is supported only for directories,
			// otherwise acl_get_file() gives EACCESS
			continue;
		}

		acl_t acl = acl_get_file(path, type);
		if (!acl) {
			error = errno;
			if (is_absence_error(error)) {
				ret = 0;
			}
			continue;
		}

		ret = bfs_check_acl_type(acl, type);
		error = errno;
		acl_free(acl);
	}
#endif

	free_fake_at(ftwbuf, path);
	errno = error;
	return ret;
}

#else // !BFS_CAN_CHECK_ACL

int bfs_check_acl(const struct BFTW *ftwbuf) {
	errno = ENOTSUP;
	return -1;
}

#endif

#if BFS_CAN_CHECK_CAPABILITIES

int bfs_check_capabilities(const struct BFTW *ftwbuf) {
	if (ftwbuf->type == BFS_LNK) {
		return 0;
	}

	int ret = -1, error;
	const char *path = fake_at(ftwbuf);

	cap_t caps = cap_get_file(path);
	if (!caps) {
		error = errno;
		if (is_absence_error(error)) {
			ret = 0;
		}
		goto out_path;
	}

	// TODO: Any better way to check for a non-empty capability set?
	char *text = cap_to_text(caps, NULL);
	if (!text) {
		error = errno;
		goto out_caps;
	}
	ret = text[0] ? 1 : 0;

	error = errno;
	cap_free(text);
out_caps:
	cap_free(caps);
out_path:
	free_fake_at(ftwbuf, path);
	errno = error;
	return ret;
}

#else // !BFS_CAN_CHECK_CAPABILITIES

int bfs_check_capabilities(const struct BFTW *ftwbuf) {
	errno = ENOTSUP;
	return -1;
}

#endif

#if BFS_CAN_CHECK_XATTRS

#if BFS_USE_SYS_EXTATTR_H

/** Wrapper for extattr_list_{file,link}. */
static ssize_t bfs_extattr_list(const char *path, enum bfs_type type, int namespace) {
	if (type == BFS_LNK) {
#if BFS_HAS_EXTATTR_LIST_LINK
		return extattr_list_link(path, namespace, NULL, 0);
#elif BFS_HAS_EXTATTR_GET_LINK
		return extattr_get_link(path, namespace, "", NULL, 0);
#else
		return 0;
#endif
	}

#if BFS_HAS_EXTATTR_LIST_FILE
	return extattr_list_file(path, namespace, NULL, 0);
#elif BFS_HAS_EXTATTR_GET_FILE
	// From man extattr(2):
	//
	//     In earlier versions of this API, passing an empty string for the
	//     attribute name to extattr_get_file() would return the list of attributes
	//     defined for the target object.  This interface has been deprecated in
	//     preference to using the explicit list API, and should not be used.
	return extattr_get_file(path, namespace, "", NULL, 0);
#else
	return 0;
#endif
}

/** Wrapper for extattr_get_{file,link}. */
static ssize_t bfs_extattr_get(const char *path, enum bfs_type type, int namespace, const char *name) {
	if (type == BFS_LNK) {
#if BFS_HAS_EXTATTR_GET_LINK
		return extattr_get_link(path, namespace, name, NULL, 0);
#else
		return 0;
#endif
	}

#if BFS_HAS_EXTATTR_GET_FILE
	return extattr_get_file(path, namespace, name, NULL, 0);
#else
	return 0;
#endif
}

#endif // BFS_USE_SYS_EXTATTR_H

int bfs_check_xattrs(const struct BFTW *ftwbuf) {
	const char *path = fake_at(ftwbuf);
	ssize_t len;

#if BFS_USE_SYS_EXTATTR_H
	len = bfs_extattr_list(path, ftwbuf->type, EXTATTR_NAMESPACE_SYSTEM);
	if (len <= 0) {
		len = bfs_extattr_list(path, ftwbuf->type, EXTATTR_NAMESPACE_USER);
	}
#elif __APPLE__
	int options = ftwbuf->type == BFS_LNK ? XATTR_NOFOLLOW : 0;
	len = listxattr(path, NULL, 0, options);
#else
	if (ftwbuf->type == BFS_LNK) {
		len = llistxattr(path, NULL, 0);
	} else {
		len = listxattr(path, NULL, 0);
	}
#endif

	int error = errno;

	free_fake_at(ftwbuf, path);

	if (len > 0) {
		return 1;
	} else if (len == 0 || is_absence_error(error)) {
		return 0;
	} else if (error == E2BIG) {
		return 1;
	} else {
		errno = error;
		return -1;
	}
}

int bfs_check_xattr_named(const struct BFTW *ftwbuf, const char *name) {
	const char *path = fake_at(ftwbuf);
	ssize_t len;

#if BFS_USE_SYS_EXTATTR_H
	len = bfs_extattr_get(path, ftwbuf->type, EXTATTR_NAMESPACE_SYSTEM, name);
	if (len < 0) {
		len = bfs_extattr_get(path, ftwbuf->type, EXTATTR_NAMESPACE_USER, name);
	}
#elif __APPLE__
	int options = ftwbuf->type == BFS_LNK ? XATTR_NOFOLLOW : 0;
	len = getxattr(path, name, NULL, 0, 0, options);
#else
	if (ftwbuf->type == BFS_LNK) {
		len = lgetxattr(path, name, NULL, 0);
	} else {
		len = getxattr(path, name, NULL, 0);
	}
#endif

	int error = errno;

	free_fake_at(ftwbuf, path);

	if (len >= 0) {
		return 1;
	} else if (is_absence_error(error)) {
		return 0;
	} else if (error == E2BIG) {
		return 1;
	} else {
		errno = error;
		return -1;
	}
}

#else // !BFS_CAN_CHECK_XATTRS

int bfs_check_xattrs(const struct BFTW *ftwbuf) {
	errno = ENOTSUP;
	return -1;
}

int bfs_check_xattr_named(const struct BFTW *ftwbuf, const char *name) {
	errno = ENOTSUP;
	return -1;
}

#endif

char *bfs_getfilecon(const struct BFTW *ftwbuf) {
#if BFS_CAN_CHECK_CONTEXT
	const char *path = fake_at(ftwbuf);

	char *con;
	int ret;
	if (ftwbuf->type == BFS_LNK) {
		ret = lgetfilecon(path, &con);
	} else {
		ret = getfilecon(path, &con);
	}

	if (ret >= 0) {
		return con;
	} else {
		return NULL;
	}
#else
	errno = ENOTSUP;
	return NULL;
#endif
}

void bfs_freecon(char *con) {
#if BFS_CAN_CHECK_CONTEXT
	freecon(con);
#endif
}
