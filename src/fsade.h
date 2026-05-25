// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

/**
 * A facade over (file)system features that are (un)implemented differently
 * between platforms.
 */

#ifndef BFS_FSADE_H
#define BFS_FSADE_H

#include "bfs.h"

#define BFS_CAN_CHECK_ACL (BFS_HAS_ACL_GET_FILE || BFS_HAS_ACL_TRIVIAL)

#define BFS_CAN_CHECK_CAPABILITIES BFS_WITH_LIBCAP

#define BFS_CAN_CHECK_CONTEXT BFS_WITH_LIBSELINUX

#if __has_include(<sys/extattr.h>) || __has_include(<sys/xattr.h>)
#  define BFS_CAN_CHECK_XATTRS true
#else
#  define BFS_CAN_CHECK_XATTRS false
#endif

struct BFTW;

/**
 * Check if a file has a non-trivial Access Control List.
 *
 * @ftwbuf
 *         The file to check.
 * @return
 *         1 if it does, 0 if it doesn't, or -1 if an error occurred.
 */
int bfs_check_acl(const struct BFTW *ftwbuf);

/**
 * Check if a file has a non-trivial capability set.
 *
 * @ftwbuf
 *         The file to check.
 * @return
 *         1 if it does, 0 if it doesn't, or -1 if an error occurred.
 */
int bfs_check_capabilities(const struct BFTW *ftwbuf);

/**
 * Check if a file has any extended attributes set.
 *
 * @ftwbuf
 *         The file to check.
 * @return
 *         1 if it does, 0 if it doesn't, or -1 if an error occurred.
 */
int bfs_check_xattrs(const struct BFTW *ftwbuf);

/**
 * Check if a file has an extended attribute with the given name.
 *
 * @ftwbuf
 *         The file to check.
 * @name
 *         The name of the xattr to check.
 * @return
 *         1 if it does, 0 if it doesn't, or -1 if an error occurred.
 */
int bfs_check_xattr_named(const struct BFTW *ftwbuf, const char *name);

/**
 * Get a file's SELinux context
 *
 * @ftwbuf
 *         The file to check.
 * @return
 *         The file's SELinux context, or NULL on failure.
 */
char *bfs_getfilecon(const struct BFTW *ftwbuf);

/**
 * Free a bfs_getfilecon() result.
 */
void bfs_freecon(char *con);

#endif // BFS_FSADE_H
