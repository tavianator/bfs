// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

/**
 * Configuration and feature/platform detection.
 */

#ifndef BFS_CONFIG_H
#define BFS_CONFIG_H

// Possible __STDC_VERSION__ values

#define C95 199409L
#define C99 199901L
#define C11 201112L
#define C17 201710L
#define C23 202311L

#include <stddef.h>

#if __STDC_VERSION__ < C23
#  include <stdalign.h>
#  include <stdbool.h>
#  include <stdnoreturn.h>
#endif

// bfs packaging configuration

#ifndef BFS_COMMAND
#  define BFS_COMMAND "bfs"
#endif
#ifndef BFS_VERSION
#  define BFS_VERSION "3.1.1"
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
#if __has_include(<threads.h>)
#  define BFS_HAS_THREADS_H true
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
#define BFS_HAS_THREADS_H (!__STDC_NO_THREADS__)
#define BFS_HAS_UTIL_H __NetBSD__

#endif // !__has_include

#ifndef BFS_USE_MNTENT_H
#  define BFS_USE_MNTENT_H BFS_HAS_MNTENT_H
#endif
#ifndef BFS_USE_PATHS_H
#  define BFS_USE_PATHS_H BFS_HAS_PATHS_H
#endif
#ifndef BFS_USE_SYS_ACL_H
#  define BFS_USE_SYS_ACL_H (BFS_HAS_SYS_ACL_H && !__illumos__)
#endif
#ifndef BFS_USE_SYS_CAPABILITY_H
#  define BFS_USE_SYS_CAPABILITY_H (BFS_HAS_SYS_CAPABILITY_H && !__FreeBSD__)
#endif
#ifndef BFS_USE_SYS_EXTATTR_H
#  define BFS_USE_SYS_EXTATTR_H (BFS_HAS_SYS_EXTATTR_H && !__DragonFly__)
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
#ifndef BFS_USE_THREADS_H
#  define BFS_USE_THREADS_H BFS_HAS_THREADS_H
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

#ifndef __NetBSD_Prereq__
#  define __NetBSD_Prereq__(maj, min, patch) false
#endif

// Fundamental utilities

/**
 * Get the length of an array.
 */
#define countof(array) (sizeof(array) / sizeof(0[array]))

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

#if __COSMOPOLITAN__
typedef long double max_align_t;
#endif

// Wrappers for attributes

/**
 * Silence warnings about switch/case fall-throughs.
 */
#if __has_c_attribute(fallthrough)
#  define fallthru [[fallthrough]]
#elif __has_attribute(fallthrough)
#  define fallthru __attribute__((fallthrough))
#else
#  define fallthru ((void)0)
#endif

/**
 * Silence warnings about unused declarations.
 */
#if __has_c_attribute(maybe_unused)
#  define attr_maybe_unused [[maybe_unused]]
#elif __has_attribute(unused)
#  define attr_maybe_unused __attribute__((unused))
#else
#  define attr_maybe_unused
#endif

/**
 * Warn if a value is unused.
 */
#if __has_c_attribute(nodiscard)
#  define attr_nodiscard [[nodiscard]]
#elif __has_attribute(warn_unused_result)
#  define attr_nodiscard __attribute__((warn_unused_result))
#else
#  define attr_nodiscard
#endif

/**
 * Hint to avoid inlining a function.
 */
#if __has_attribute(noinline)
#  define attr_noinline __attribute__((noinline))
#else
#  define attr_noinline
#endif

/**
 * Hint that a function is unlikely to be called.
 */
#if __has_attribute(cold)
#  define attr_cold attr_noinline __attribute__((cold))
#else
#  define attr_cold attr_noinline
#endif

/**
 * Adds compiler warnings for bad printf()-style function calls, if supported.
 */
#if __has_attribute(format)
#  define attr_printf(fmt, args) __attribute__((format(printf, fmt, args)))
#else
#  define attr_printf(fmt, args)
#endif

/**
 * Annotates allocator-like functions.
 */
#if __has_attribute(malloc)
#  if __GNUC__ >= 11
#    define attr_malloc(...) attr_nodiscard __attribute__((malloc(__VA_ARGS__)))
#  else
#    define attr_malloc(...) attr_nodiscard __attribute__((malloc))
#  endif
#else
#  define attr_malloc(...) attr_nodiscard
#endif

/**
 * Specifies that a function returns allocations with a given alignment.
 */
#if __has_attribute(alloc_align)
#  define attr_alloc_align(param) __attribute__((alloc_align(param)))
#else
#  define attr_alloc_align(param)
#endif

/**
 * Specifies that a function returns allocations with a given size.
 */
#if __has_attribute(alloc_size)
#  define attr_alloc_size(...) __attribute__((alloc_size(__VA_ARGS__)))
#else
#  define attr_alloc_size(...)
#endif

/**
 * Shorthand for attr_alloc_align() and attr_alloc_size().
 */
#define attr_aligned_alloc(align, ...) \
	attr_alloc_align(align) \
	attr_alloc_size(__VA_ARGS__)

/**
 * Check if function multiversioning via GNU indirect functions (ifunc) is supported.
 */
#ifndef BFS_USE_TARGET_CLONES
#  if __has_attribute(target_clones) && (__GLIBC__ || __FreeBSD__)
#    define BFS_USE_TARGET_CLONES true
#  endif
#endif

/**
 * Apply the target_clones attribute, if available.
 */
#if BFS_USE_TARGET_CLONES
#  define attr_target_clones(...) __attribute__((target_clones(__VA_ARGS__)))
#else
#  define attr_target_clones(...)
#endif

/**
 * Shorthand for multiple attributes at once. attr(a, b(c), d) is equivalent to
 *
 *     attr_a
 *     attr_b(c)
 *     attr_d
 */
#define attr(...) \
	attr__(attr_##__VA_ARGS__, none, none, none, none, none, none, none, none, none)

/**
 * attr() helper.  For exposition, pretend we support only 2 args, instead of 9.
 * There are a few cases:
 *
 *     attr()
 *         => attr__(attr_, none, none)
 *         => attr_                =>
 *            attr_none            =>
 *            attr_too_many_none() =>
 *
 *     attr(a)
 *         => attr__(attr_a, none, none)
 *         => attr_a               => __attribute__((a))
 *            attr_none            =>
 *            attr_too_many_none() =>
 *
 *     attr(a, b(c))
 *         => attr__(attr_a, b(c), none, none)
 *         => attr_a                   => __attribute__((a))
 *            attr_b(c)                => __attribute__((b(c)))
 *            attr_too_many_none(none) =>
 *
 *     attr(a, b(c), d)
 *         => attr__(attr_a, b(c), d, none, none)
 *         => attr_a                      => __attribute__((a))
 *            attr_b(c)                   => __attribute__((b(c)))
 *            attr_too_many_d(none, none) => error
 *
 * Some attribute names are the same as standard library functions, e.g. printf.
 * Standard libraries are permitted to define these functions as macros, like
 *
 *     #define printf(...) __builtin_printf(__VA_ARGS__)
 *
 * The token paste in
 *
 *     #define attr(...) attr__(attr_##__VA_ARGS__, none, none)
 *
 * is necessary to prevent macro expansion before evaluating attr__().
 * Otherwise, we could get
 *
 *     attr(printf(1, 2))
 *         => attr__(__builtin_printf(1, 2), none, none)
 *         => attr____builtin_printf(1, 2)
 *         => error
 */
#define attr__(a1, a2, a3, a4, a5, a6, a7, a8, a9, none, ...) \
	a1 \
	attr_##a2 \
	attr_##a3 \
	attr_##a4 \
	attr_##a5 \
	attr_##a6 \
	attr_##a7 \
	attr_##a8 \
	attr_##a9 \
	attr_too_many_##none(__VA_ARGS__)

// Ignore `attr_none` from expanding 1-9 argument attr(a1, a2, ...)
#define attr_none
// Ignore `attr_` from expanding 0-argument attr()
#define attr_
// Only trigger an error on more than 9 arguments
#define attr_too_many_none(...)

#endif // BFS_CONFIG_H
