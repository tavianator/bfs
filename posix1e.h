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

#ifndef BFS_POSIX1E_H
#define BFS_POSIX1E_H

#include "bftw.h"
#include "util.h"
#include <stdbool.h>

#if BFS_HAS_SYS_CAPABILITY && !__FreeBSD__
#	include <sys/capability.h>
#	ifdef CAP_CHOWN
#		define BFS_HAS_POSIX1E_CAPABILITIES true
#	endif
#endif

/**
 * Check if a file has a non-trvial Access Control List.
 */
bool bfs_check_acl(const struct BFTW *ftwbuf);

/**
 * Check if a file has a non-trvial capability set.
 */
bool bfs_check_capabilities(const struct BFTW *ftwbuf);

#endif // BFS_POSIX1E_H
