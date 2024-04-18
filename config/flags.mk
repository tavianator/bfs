# Copyright © Tavian Barnes <tavianator@tavianator.com>
# SPDX-License-Identifier: 0BSD

# Makefile that generates gen/flags.mk

include config/prelude.mk
include ${GEN}/vars.mk

# Configurable flags
CPPFLAGS ?=
CFLAGS ?= \
    -g \
    -Wall \
    -Wformat=2 \
    -Werror=implicit \
    -Wimplicit-fallthrough \
    -Wmissing-declarations \
    -Wshadow \
    -Wsign-compare \
    -Wstrict-prototypes
LDFLAGS ?=
LDLIBS ?=

export XCPPFLAGS=${CPPFLAGS}
export XCFLAGS=${CFLAGS}
export XLDFLAGS=${LDFLAGS}
export XLDLIBS=${LDLIBS}

# Immutable flags
export BFS_CPPFLAGS= \
    -Isrc \
    -I${GEN} \
    -D__EXTENSIONS__ \
    -D_ATFILE_SOURCE \
    -D_BSD_SOURCE \
    -D_DARWIN_C_SOURCE \
    -D_DEFAULT_SOURCE \
    -D_GNU_SOURCE \
    -D_POSIX_PTHREAD_SEMANTICS \
    -D_FILE_OFFSET_BITS=64 \
    -D_TIME_BITS=64
export BFS_CFLAGS= -std=c17 -pthread

# Platform-specific system libraries
LDLIBS,DragonFly := -lposix1e
LDLIBS,Linux := -lrt
LDLIBS,NetBSD := -lutil
LDLIBS,SunOS := -lsocket -lnsl
export BFS_LDLIBS=${LDLIBS,${OS}}

# Build profiles
_ASAN := ${TRUTHY,${ASAN}}
_LSAN := ${TRUTHY,${LSAN}}
_MSAN := ${TRUTHY,${MSAN}}
_TSAN := ${TRUTHY,${TSAN}}
_UBSAN := ${TRUTHY,${UBSAN}}
_GCOV := ${TRUTHY,${GCOV}}
_LINT := ${TRUTHY,${LINT}}
_RELEASE := ${TRUTHY,${RELEASE}}

# https://github.com/google/sanitizers/issues/342
TSAN_CPPFLAGS,y := -DBFS_USE_TARGET_CLONES=0
export TSAN_CPPFLAGS=${TSAN_CPPFLAGS,${_TSAN}}

ASAN_CFLAGS,y := -fsanitize=address
LSAN_CFLAGS,y := -fsanitize=leak
MSAN_CFLAGS,y := -fsanitize=memory -fsanitize-memory-track-origins
TSAN_CFLAGS,y := -fsanitize=thread
UBSAN_CFLAGS.y := -fsanitize=undefined

export ASAN_CFLAGS=${ASAN_CFLAGS,${_ASAN}}
export LSAN_CFLAGS=${LSAN_CFLAGS,${_LSAN}}
export MSAN_CFLAGS=${MSAN_CFLAGS,${_MSAN}}
export TSAN_CFLAGS=${TSAN_CFLAGS,${_TSAN}}
export UBSAN_CFLAGS=${UBSAN_CFLAGS,${_UBSAN}}

SAN_CFLAGS,y := -fno-sanitize-recover=all
NO_SAN := ${NOR,${_ASAN},${_LSAN},${_MSAN},${_TSAN},${_UBSAN}}
SAN := ${NOT,${NO_SAN}}
export SAN_CFLAGS=${SAN_CFLAGS,${SAN}}

# MSAN and TSAN both need all code to be instrumented
NOLIBS ?= ${NOT,${NOR,${_MSAN},${_TSAN}}}
export XNOLIBS=${NOLIBS}

# gcov only intercepts fork()/exec() with -std=gnu*
GCOV_CFLAGS,y := -std=gnu17 --coverage
export GCOV_CFLAGS=${GCOV_CFLAGS,${_GCOV}}

LINT_CPPFLAGS,y := -D_FORTIFY_SOURCE=3 -DBFS_LINT
LINT_CFLAGS,y := -Werror -O2

export LINT_CPPFLAGS=${LINT_CPPFLAGS,${_LINT}}
export LINT_CFLAGS=${LINT_CFLAGS,${_LINT}}

RELEASE_CPPFLAGS,y := -DNDEBUG
RELEASE_CFLAGS,y := -O3 -flto=auto

export RELEASE_CPPFLAGS=${RELEASE_CPPFLAGS,${_RELEASE}}
export RELEASE_CFLAGS=${RELEASE_CFLAGS,${_RELEASE}}

# Set a variable
SETVAR = printf '%s := %s\n' >>$@

# Append to a variable, if non-empty
APPEND = append() { test -z "$$2" || printf '%s += %s\n' "$$1" "$$2" >>$@; }; append

${GEN}/flags.mk::
	${MSG} "[ GEN] ${TGT}"
	printf '# %s\n' "${TGT}" >$@
	${SETVAR} CPPFLAGS "$$BFS_CPPFLAGS"
	${APPEND} CPPFLAGS "$$TSAN_CPPFLAGS"
	${APPEND} CPPFLAGS "$$LINT_CPPFLAGS"
	${APPEND} CPPFLAGS "$$RELEASE_CPPFLAGS"
	${APPEND} CPPFLAGS "$$XCPPFLAGS"
	${APPEND} CPPFLAGS "$$EXTRA_CPPFLAGS"
	${SETVAR} CFLAGS "$$BFS_CFLAGS"
	${APPEND} CFLAGS "$$ASAN_CFLAGS"
	${APPEND} CFLAGS "$$LSAN_CFLAGS"
	${APPEND} CFLAGS "$$MSAN_CFLAGS"
	${APPEND} CFLAGS "$$TSAN_CFLAGS"
	${APPEND} CFLAGS "$$UBSAN_CFLAGS"
	${APPEND} CFLAGS "$$SAN_CFLAGS"
	${APPEND} CFLAGS "$$GCOV_CFLAGS"
	${APPEND} CFLAGS "$$LINT_CFLAGS"
	${APPEND} CFLAGS "$$RELEASE_CFLAGS"
	${APPEND} CFLAGS "$$XCFLAGS"
	${APPEND} CFLAGS "$$EXTRA_CFLAGS"
	${SETVAR} LDFLAGS "$$XLDFLAGS"
	${SETVAR} LDLIBS "$$XLDLIBS"
	${APPEND} LDLIBS "$$EXTRA_LDLIBS"
	${APPEND} LDLIBS "$$BFS_LDLIBS"
	${SETVAR} NOLIBS "$$XNOLIBS"
	test "${OS}-${SAN}" != FreeBSD-y || printf 'POSTLINK = elfctl -e +noaslr $$@\n' >>$@
	${VCAT} $@
