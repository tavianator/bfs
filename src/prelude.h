// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

/**
 * Praeludium.
 *
 * This header is automatically included in every translation unit, before any
 * other headers, so it can set feature test macros[1][2].  This sets up our own
 * mini-dialect of C, which includes
 *
 *   - Standard C17 and POSIX.1 2024 features
 *   - Portable and platform-specific extensions
 *   - Convenience macros like `bool`, `alignof`, etc.
 *   - Common compiler extensions like __has_include()
 *
 * Further bfs-specific utilities are defined in "bfs.h".
 *
 * [1]: https://www.gnu.org/software/libc/manual/html_node/Feature-Test-Macros.html
 * [2]: https://pubs.opengroup.org/onlinepubs/9799919799/functions/V2_chap02.html
 */

#ifndef BFS_PRELUDE_H
#define BFS_PRELUDE_H

// Feature test macros

/**
 * Linux and BSD handle _POSIX_C_SOURCE differently: on Linux, it enables POSIX
 * interfaces that are not visible by default.  On BSD, it also *disables* most
 * extensions, giving a strict POSIX environment.  Since we want the extensions,
 * we don't set _POSIX_C_SOURCE.
 */
// #define _POSIX_C_SOURCE 202405L

/** openat() etc. */
#define _ATFILE_SOURCE 1

/** BSD-derived extensions. */
#define _BSD_SOURCE 1

/** glibc successor to _BSD_SOURCE. */
#define _DEFAULT_SOURCE 1

/** GNU extensions. */
#define _GNU_SOURCE 1

/** Use 64-bit off_t. */
#define _FILE_OFFSET_BITS 64

/** Use 64-bit time_t. */
#define _TIME_BITS 64

/** macOS extensions. */
#if __APPLE__
#  define _DARWIN_C_SOURCE 1
#endif

/** Solaris extensions. */
#if __sun
#  define __EXTENSIONS__ 1
// https://illumos.org/man/3C/getpwnam#standard-conforming
#  define _POSIX_PTHREAD_SEMANTICS 1
#endif

// Get the convenience macros that became standard spellings in C23
#if __STDC_VERSION__ < 202311L

/** _Static_assert() => static_assert() */
#include <assert.h>
/** _Alignas(), _Alignof() => alignas(), alignof() */
#include <stdalign.h>
/** _Bool => bool, true, false */
#include <stdbool.h>

/**
 * C23 deprecates `noreturn void` in favour of `[[noreturn]] void`, so we expose
 * _noreturn instead with the other attributes in "bfs.h".
 */
// #include <stdnoreturn.h>

/** Part of <threads.h>, but we don't use anything else from it. */
#define thread_local _Thread_local

#endif // !C23

// Feature detection

// https://clang.llvm.org/docs/LanguageExtensions.html#has-attribute
#ifndef __has_attribute
#  define __has_attribute(attr) false
#endif

// https://clang.llvm.org/docs/LanguageExtensions.html#has-builtin
#ifndef __has_builtin
#  define __has_builtin(builtin) false
#endif

// https://en.cppreference.com/w/c/language/attributes#Attribute_testing
#ifndef __has_c_attribute
#  define __has_c_attribute(attr) false
#endif

// https://clang.llvm.org/docs/LanguageExtensions.html#has-feature-and-has-extension
#ifndef __has_feature
#  define __has_feature(feat) false
#endif

// https://en.cppreference.com/w/c/preprocessor/include
#ifndef __has_include
#  define __has_include(header) false
#endif

// Sanitizer macros (GCC defines these but Clang does not)

#if __has_feature(address_sanitizer) && !defined(__SANITIZE_ADDRESS__)
#  define __SANITIZE_ADDRESS__ true
#endif
#if __has_feature(memory_sanitizer) && !defined(__SANITIZE_MEMORY__)
#  define __SANITIZE_MEMORY__ true
#endif
#if __has_feature(thread_sanitizer) && !defined(__SANITIZE_THREAD__)
#  define __SANITIZE_THREAD__ true
#endif

#endif // BFS_PRELUDE_H
