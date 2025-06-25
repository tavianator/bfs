// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

/**
 * Configuration and fundamental utilities.
 */

#ifndef BFS_H
#define BFS_H

// Standard versions

/** Possible __STDC_VERSION__ values. */
#define C95 199409L
#define C99 199901L
#define C11 201112L
#define C17 201710L
#define C23 202311L

/** Possible _POSIX_C_SOURCE and _POSIX_<OPTION> values. */
#define POSIX_1990 1
#define POSIX_1992 2
#define POSIX_1993 199309L
#define POSIX_1995 199506L
#define POSIX_2001 200112L
#define POSIX_2008 200809L
#define POSIX_2024 202405L

// Build configuration

#include "config.h"

#ifndef BFS_COMMAND
#  define BFS_COMMAND "bfs"
#endif

#ifndef BFS_HOMEPAGE
#  define BFS_HOMEPAGE "https://tavianator.com/projects/bfs.html"
#endif

#ifndef BFS_LINT
#  define BFS_LINT false
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

// Get __GLIBC__
#include <assert.h>

// Fundamental utilities

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
 * Silence warnings about unused declarations.
 */
#if __has_c_attribute(maybe_unused)
#  define _maybe_unused maybe_unused
#elif __has_c_attribute(gnu::unused)
#  define _maybe_unused gnu::unused
#else
#  define _maybe_unused
#endif

/**
 * Warn if a value is unused.
 */
#if __has_c_attribute(nodiscard)
#  define _nodiscard nodiscard
#elif __has_c_attribute(gnu::warn_unused_result)
#  define _nodiscard gnu::warn_unused_result
#else
#  define _nodiscard
#endif

/**
 * Hint to avoid inlining a function.
 */
#if __has_c_attribute(gnu::noinline)
#  define _noinline gnu::noinline
#else
#  define _noinline
#endif

/**
 * Hint that a function is unlikely to be called.
 */
#if __has_c_attribute(gnu::cold)
#  define _cold _noinline, gnu::cold
#else
#  define _cold _noinline
#endif

/**
 * Marks a non-returning function.
 */
#if __has_c_attribute(noreturn)
#  define _noreturn noreturn
#elif __has_c_attribute(gnu::noreturn)
#  define _noreturn gnu::noreturn
#else
#  define _noreturn
#endif


/**
 * Adds compiler warnings for bad printf()-style function calls, if supported.
 */
#if __has_c_attribute(gnu::format)
#  define _printf(fmt, args) gnu::format(printf, fmt, args)
#else
#  define _printf(fmt, args)
#endif

/**
 * Annotates functions that potentially modify and return format strings.
 */
#if __has_c_attribute(gnu::format_arg)
#  define _format_arg(arg) gnu::format_arg(arg)
#else
#  define _format_arg(arg)
#endif

/**
 * Annotates allocator-like functions.
 */
#if __has_c_attribute(gnu::malloc)
#  if __GNUC__ >= 11 && !__OPTIMIZE__ // malloc(deallocator) disables inlining on GCC
#    define _malloc(...) _nodiscard, gnu::malloc(__VA_ARGS__)
#  else
#    define _malloc(...) _nodiscard, gnu::malloc
#  endif
#else
#  define _malloc(...) _nodiscard
#endif

/**
 * Specifies that a function returns allocations with a given alignment.
 */
#if __has_c_attribute(gnu::alloc_align)
#  define _alloc_align(param) gnu::alloc_align(param)
#else
#  define _alloc_align(param)
#endif

/**
 * Specifies that a function returns allocations with a given size.
 */
#if __has_c_attribute(gnu::alloc_size)
#  define _alloc_size(...) gnu::alloc_size(__VA_ARGS__)
#else
#  define _alloc_size(...)
#endif

/**
 * Shorthand for _alloc_align() and _alloc_size().
 */
#define _aligned_alloc(align, ...) _alloc_align(align), _alloc_size(__VA_ARGS__)

/**
 * Check if function multiversioning via GNU indirect functions (ifunc) is supported.
 *
 * Disabled on TSan due to https://github.com/google/sanitizers/issues/342.
 */
#ifndef BFS_USE_TARGET_CLONES
#  if __has_c_attribute(gnu::target_clones) \
	&& (__GLIBC__ || __FreeBSD__) \
	&& !__SANITIZE_THREAD__ \
	&& !__SANITIZE_TYPE__
#    define BFS_USE_TARGET_CLONES true
#  else
#    define BFS_USE_TARGET_CLONES false
#  endif
#endif

/**
 * Apply the target_clones attribute, if available.
 */
#if BFS_USE_TARGET_CLONES
#  define _target_clones(...) gnu::target_clones(__VA_ARGS__)
#else
#  define _target_clones(...)
#endif

/**
 * Mark the size of a flexible array member.
 */
#if __has_c_attribute(clang::counted_by)
#  define _counted_by(...) clang::counted_by(__VA_ARGS__)
#elif __has_c_attribute(gnu::counted_by)
#  define _counted_by(...) gnu::counted_by(__VA_ARGS__)
#else
#  define _counted_by(...)
#endif

/**
 * Optimization hint to not unroll a loop.
 */
#if BFS_HAS_PRAGMA_NOUNROLL
#  define _nounroll _Pragma("nounroll")
#elif __GNUC__ && !__clang__
#  define _nounroll _Pragma("GCC unroll 0")
#else
#  define _nounroll
#endif

#endif // BFS_H
