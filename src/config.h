// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

/**
 * Configuration and feature/platform detection.
 */

#ifndef BFS_CONFIG_H
#define BFS_CONFIG_H

#include <stddef.h>

#if __STDC_VERSION__ < 202311L
#  include <stdalign.h>
#  include <stdbool.h>
#  include <stdnoreturn.h>
#endif

// bfs packaging configuration

#ifndef BFS_COMMAND
#  define BFS_COMMAND "bfs"
#endif
#ifndef BFS_VERSION
#  define BFS_VERSION "2.6.3"
#endif
#ifndef BFS_HOMEPAGE
#  define BFS_HOMEPAGE "https://tavianator.com/projects/bfs.html"
#endif

// Check for system headers

#ifdef __has_include

#if __has_include(<mntent.h>)
#  define BFS_HAS_MNTENT_H true
#endif
#if __has_include(<paths.h>)
#  define BFS_HAS_PATHS_H true
#endif
#if __has_include(<sys/acl.h>)
#  define BFS_HAS_SYS_ACL_H true
#endif
#if __has_include(<sys/capability.h>)
#  define BFS_HAS_SYS_CAPABILITY_H true
#endif
#if __has_include(<sys/extattr.h>)
#  define BFS_HAS_SYS_EXTATTR_H true
#endif
#if __has_include(<sys/mkdev.h>)
#  define BFS_HAS_SYS_MKDEV_H true
#endif
#if __has_include(<sys/param.h>)
#  define BFS_HAS_SYS_PARAM_H true
#endif
#if __has_include(<sys/sysmacros.h>)
#  define BFS_HAS_SYS_SYSMACROS_H true
#endif
#if __has_include(<sys/xattr.h>)
#  define BFS_HAS_SYS_XATTR_H true
#endif
#if __has_include(<util.h>)
#  define BFS_HAS_UTIL_H true
#endif

#else // !__has_include

#define BFS_HAS_MNTENT_H __GLIBC__
#define BFS_HAS_PATHS_H true
#define BFS_HAS_SYS_ACL_H true
#define BFS_HAS_SYS_CAPABILITY_H __linux__
#define BFS_HAS_SYS_EXTATTR_H __FreeBSD__
#define BFS_HAS_SYS_MKDEV_H false
#define BFS_HAS_SYS_PARAM_H true
#define BFS_HAS_SYS_SYSMACROS_H __GLIBC__
#define BFS_HAS_SYS_XATTR_H __linux__
#define BFS_HAS_UTIL_H __NetBSD__

#endif // !__has_include

#ifndef BFS_USE_MNTENT_H
#  define BFS_USE_MNTENT_H BFS_HAS_MNTENT_H
#endif
#ifndef BFS_USE_PATHS_H
#  define BFS_USE_PATHS_H BFS_HAS_PATHS_H
#endif
#ifndef BFS_USE_SYS_ACL_H
#  define BFS_USE_SYS_ACL_H BFS_HAS_SYS_ACL_H
#endif
#ifndef BFS_USE_SYS_CAPABILITY_H
#  define BFS_USE_SYS_CAPABILITY_H BFS_HAS_SYS_CAPABILITY_H
#endif
#ifndef BFS_USE_SYS_EXTATTR_H
#  define BFS_USE_SYS_EXTATTR_H BFS_HAS_SYS_EXTATTR_H
#endif
#ifndef BFS_USE_SYS_MKDEV_H
#  define BFS_USE_SYS_MKDEV_H BFS_HAS_SYS_MKDEV_H
#endif
#ifndef BFS_USE_SYS_PARAM_H
#  define BFS_USE_SYS_PARAM_H BFS_HAS_SYS_PARAM_H
#endif
#ifndef BFS_USE_SYS_SYSMACROS_H
#  define BFS_USE_SYS_SYSMACROS_H BFS_HAS_SYS_SYSMACROS_H
#endif
#ifndef BFS_USE_SYS_XATTR_H
#  define BFS_USE_SYS_XATTR_H BFS_HAS_SYS_XATTR_H
#endif
#ifndef BFS_USE_UTIL_H
#  define BFS_USE_UTIL_H BFS_HAS_UTIL_H
#endif

// Stub out feature detection on old/incompatible compilers

#ifndef __has_feature
#  define __has_feature(feat) false
#endif

#ifndef __has_c_attribute
#  define __has_c_attribute(attr) false
#endif

#ifndef __has_attribute
#  define __has_attribute(attr) false
#endif

// Platform detection

// Get the definition of BSD if available
#if BFS_USE_SYS_PARAM_H
#  include <sys/param.h>
#endif

#ifndef __GLIBC_PREREQ
#  define __GLIBC_PREREQ(maj, min) false
#endif

// Fundamental utilities

/**
 * Get the length of an array.
 */
#define countof(array) (sizeof(array) / sizeof(0[array]))

/**
 * Round down to a multiple of an alignment.
 */
static inline size_t align_floor(size_t align, size_t size) {
	return size & ~(align - 1);
}

/**
 * Round up to a multiple of an alignment.
 */
static inline size_t align_ceil(size_t align, size_t size) {
	return align_floor(align, size + align - 1);
}

/**
 * Computes the size of a struct containing a flexible array member of the given
 * length.
 *
 * @param type
 *         The type of the struct containing the flexible array.
 * @param member
 *         The name of the flexible array member.
 * @param count
 *         The length of the flexible array.
 */
#define flex_sizeof(type, member, count) \
	flex_sizeof_impl(alignof(type), sizeof(type), offsetof(type, member), sizeof(((type *)NULL)->member[0]), count)

static inline size_t flex_sizeof_impl(size_t align, size_t min, size_t offset, size_t size, size_t count) {
	size_t ret = size * count;
	size_t overflow = ret / size != count;

	ret += offset;
	overflow |= ret < offset;

	size_t mask = align - 1;
	ret += mask;
	overflow |= ret < mask;
	ret |= -overflow;
	ret &= ~mask;

	// Make sure flex_sizeof(type, member, 0) >= sizeof(type), even if the
	// type has more padding than necessary for alignment
	if (min > align_ceil(align, offset) && ret < min) {
		ret = min;
	}

	return ret;
}

/**
 * False sharing/destructive interference/largest cache line size.
 */
#ifdef __GCC_DESTRUCTIVE_SIZE
#  define FALSE_SHARING_SIZE __GCC_DESTRUCTIVE_SIZE
#else
#  define FALSE_SHARING_SIZE 64
#endif

/**
 * True sharing/constructive interference/smallest cache line size.
 */
#ifdef __GCC_CONSTRUCTIVE_SIZE
#  define TRUE_SHARING_SIZE __GCC_CONSTRUCTIVE_SIZE
#else
#  define TRUE_SHARING_SIZE 64
#endif

/**
 * Alignment specifier that avoids false sharing.
 */
#define cache_align alignas(FALSE_SHARING_SIZE)

// Wrappers for attributes

/**
 * Silence compiler warnings about switch/case fall-throughs.
 */
#if __has_c_attribute(fallthrough)
#  define fallthru [[fallthrough]]
#elif __has_attribute(fallthrough)
#  define fallthru __attribute__((fallthrough))
#else
#  define fallthru ((void)0)
#endif

/**
 * Adds compiler warnings for bad printf()-style function calls, if supported.
 */
#if __has_attribute(format)
#  define BFS_FORMATTER(fmt, args) __attribute__((format(printf, fmt, args)))
#else
#  define BFS_FORMATTER(fmt, args)
#endif

/**
 * Check if function multiversioning via GNU indirect functions (ifunc) is supported.
 */
#if !defined(BFS_TARGET_CLONES) && __has_attribute(target_clones) && (__GLIBC__ || __FreeBSD__ || __NetBSD__)
#  define BFS_TARGET_CLONES true
#endif

/**
 * Ignore a particular GCC warning for a region of code.
 */
#if __GNUC__
#  define BFS_PRAGMA_STRINGIFY(...) _Pragma(#__VA_ARGS__)
#  define BFS_SUPPRESS(warning) \
	_Pragma("GCC diagnostic push"); \
	BFS_PRAGMA_STRINGIFY(GCC diagnostic ignored warning)
#  define BFS_UNSUPPRESS() \
	_Pragma("GCC diagnostic pop")
#else
#  define BFS_SUPPRESS(warning)
#  define BFS_UNSUPPRESS()
#endif

#endif // BFS_CONFIG_H
