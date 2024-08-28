// Copyright © Tavian Barnes <tavianator@tavianator.com>
// SPDX-License-Identifier: 0BSD

/**
 * Configuration and feature/platform detection.
 */

#ifndef BFS_PRELUDE_H
#define BFS_PRELUDE_H

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
 * _noreturn instead with the other attributes.
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
