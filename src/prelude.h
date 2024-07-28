// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

/**
 * Configuration and feature/platform detection.
 */

#ifndef BFS_PRELUDE_H
#define BFS_PRELUDE_H

// Possible __STDC_VERSION__ values

#define C95 199409L
#define C99 199901L
#define C11 201112L
#define C17 201710L
#define C23 202311L

// Get the static_assert() definition as well as __GLIBC__
#include <assert.h>

#if __STDC_VERSION__ < C23
#  include <stdalign.h>
#  include <stdbool.h>
#endif

// bfs packaging configuration

#include "config.h"

#ifndef BFS_COMMAND
#  define BFS_COMMAND "bfs"
#endif
#ifndef BFS_HOMEPAGE
#  define BFS_HOMEPAGE "https://tavianator.com/projects/bfs.html"
#endif

// This is a symbol instead of a literal so we don't have to rebuild everything
// when the version number changes
extern const char bfs_version[];

extern const char bfs_confflags[];
extern const char bfs_cc[];
extern const char bfs_cppflags[];
extern const char bfs_cflags[];
extern const char bfs_ldflags[];
extern const char bfs_ldlibs[];

// Check for system headers

#ifdef __has_include

#if __has_include(<mntent.h>)
#  define BFS_HAS_MNTENT_H true
#endif
#if __has_include(<paths.h>)
#  define BFS_HAS_PATHS_H true
#endif
#if __has_include(<stdbit.h>)
#  define BFS_HAS_STDBIT_H true
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
#define BFS_HAS_STDBIT_H (__STDC_VERSION__ >= C23)
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

// Fundamental utilities

/**
 * Get the length of an array.
 */
#define countof(...) (sizeof(__VA_ARGS__) / sizeof(0[__VA_ARGS__]))

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
 * Silence warnings about switch/case fall-throughs.
 */
#if __has_attribute(fallthrough)
#  define fallthru __attribute__((fallthrough))
#else
#  define fallthru ((void)0)
#endif

/**
 * Silence warnings about unused declarations.
 */
#if __has_attribute(unused)
#  define _maybe_unused __attribute__((unused))
#else
#  define _maybe_unused
#endif

/**
 * Warn if a value is unused.
 */
#if __has_attribute(warn_unused_result)
#  define _nodiscard __attribute__((warn_unused_result))
#else
#  define _nodiscard
#endif

/**
 * Hint to avoid inlining a function.
 */
#if __has_attribute(noinline)
#  define _noinline __attribute__((noinline))
#else
#  define _noinline
#endif

/**
 * Marks a non-returning function.
 */
#if __STDC_VERSION__ >= C23
#  define _noreturn [[noreturn]]
#else
#  define _noreturn _Noreturn
#endif

/**
 * Hint that a function is unlikely to be called.
 */
#if __has_attribute(cold)
#  define _cold _noinline __attribute__((cold))
#else
#  define _cold _noinline
#endif

/**
 * Adds compiler warnings for bad printf()-style function calls, if supported.
 */
#if __has_attribute(format)
#  define _printf(fmt, args) __attribute__((format(printf, fmt, args)))
#else
#  define _printf(fmt, args)
#endif

/**
 * Annotates functions that potentially modify and return format strings.
 */
#if __has_attribute(format_arg)
#  define _format_arg(arg) __attribute__((format_arg(arg)))
#else
#  define _format_arg(arg)
#endif

/**
 * Annotates allocator-like functions.
 */
#if __has_attribute(malloc)
#  if __GNUC__ >= 11 && !__OPTIMIZE__ // malloc(deallocator) disables inlining on GCC
#    define _malloc(...) _nodiscard __attribute__((malloc(__VA_ARGS__)))
#  else
#    define _malloc(...) _nodiscard __attribute__((malloc))
#  endif
#else
#  define _malloc(...) _nodiscard
#endif

/**
 * Specifies that a function returns allocations with a given alignment.
 */
#if __has_attribute(alloc_align)
#  define _alloc_align(param) __attribute__((alloc_align(param)))
#else
#  define _alloc_align(param)
#endif

/**
 * Specifies that a function returns allocations with a given size.
 */
#if __has_attribute(alloc_size)
#  define _alloc_size(...) __attribute__((alloc_size(__VA_ARGS__)))
#else
#  define _alloc_size(...)
#endif

/**
 * Shorthand for _alloc_align() and _alloc_size().
 */
#define _aligned_alloc(align, ...) _alloc_align(align) _alloc_size(__VA_ARGS__)

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
#  define _target_clones(...) __attribute__((target_clones(__VA_ARGS__)))
#else
#  define _target_clones(...)
#endif

#endif // BFS_PRELUDE_H
