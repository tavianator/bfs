# Copyright © Tavian Barnes <tavianator@tavianator.com>
# SPDX-License-Identifier: 0BSD

# Makefile that generates gen/flags.mk

include build/prelude.mk
include gen/vars.mk

# Internal flags
_CPPFLAGS := \
    -Isrc \
    -Igen \
    -D__EXTENSIONS__ \
    -D_ATFILE_SOURCE \
    -D_BSD_SOURCE \
    -D_DARWIN_C_SOURCE \
    -D_DEFAULT_SOURCE \
    -D_GNU_SOURCE \
    -D_POSIX_PTHREAD_SEMANTICS \
    -D_FILE_OFFSET_BITS=64 \
    -D_TIME_BITS=64

_CFLAGS := -std=c17 -pthread
_LDFLAGS :=
_LDLIBS :=

# Platform-specific system libraries
LDLIBS,DragonFly := -lposix1e
LDLIBS,Linux := -lrt
LDLIBS,NetBSD := -lutil
LDLIBS,SunOS := -lsec -lsocket -lnsl
_LDLIBS += ${LDLIBS,${OS}}

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
_CPPFLAGS += ${TSAN_CPPFLAGS,${_TSAN}}

ASAN_CFLAGS,y := -fsanitize=address
LSAN_CFLAGS,y := -fsanitize=leak
MSAN_CFLAGS,y := -fsanitize=memory -fsanitize-memory-track-origins
TSAN_CFLAGS,y := -fsanitize=thread
UBSAN_CFLAGS,y := -fsanitize=undefined

_CFLAGS += ${ASAN_CFLAGS,${_ASAN}}
_CFLAGS += ${LSAN_CFLAGS,${_LSAN}}
_CFLAGS += ${MSAN_CFLAGS,${_MSAN}}
_CFLAGS += ${TSAN_CFLAGS,${_TSAN}}
_CFLAGS += ${UBSAN_CFLAGS,${_UBSAN}}

SAN_CFLAGS,y := -fno-sanitize-recover=all
INSANE := ${NOT,${_ASAN}${_LSAN}${_MSAN}${_TSAN}${_UBSAN}}
SAN := ${NOT,${INSANE}}
_CFLAGS += ${SAN_CFLAGS,${SAN}}

# MSAN and TSAN both need all code to be instrumented
YESLIBS := ${NOT,${_MSAN}${_TSAN}}
NOLIBS ?= ${NOT,${YESLIBS}}

# gcov only intercepts fork()/exec() with -std=gnu*
GCOV_CFLAGS,y := -std=gnu17 --coverage
_CFLAGS += ${GCOV_CFLAGS,${_GCOV}}

LINT_CPPFLAGS,y := -D_FORTIFY_SOURCE=3 -DBFS_LINT
LINT_CFLAGS,y := -Werror -O2

_CPPFLAGS += ${LINT_CPPFLAGS,${_LINT}}
_CFLAGS += ${LINT_CFLAGS,${_LINT}}

RELEASE_CPPFLAGS,y := -DNDEBUG
RELEASE_CFLAGS,y := -O3 -flto=auto

_CPPFLAGS += ${RELEASE_CPPFLAGS,${_RELEASE}}
_CFLAGS += ${RELEASE_CFLAGS,${_RELEASE}}

# Configurable flags
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

# Add the configurable flags last so they can override ours
_CPPFLAGS += ${CPPFLAGS} ${EXTRA_CPPFLAGS}
_CFLAGS += ${CFLAGS} ${EXTRA_CFLAGS}
_LDFLAGS += ${LDFLAGS} ${EXTRA_LDFLAGS}
# (except LDLIBS, as earlier libs override later ones)
_LDLIBS := ${LDLIBS} ${EXTRA_LDLIBS} ${_LDLIBS}

include build/exports.mk

# Conditionally-supported flags
AUTO_FLAGS := \
    gen/flags/deps.mk \
    gen/flags/missing-var-decls.mk

gen/flags.mk: ${AUTO_FLAGS}
	${MSG} "[ GEN] $@"
	@printf '# %s\n' "$@" >$@
	@printf '_CPPFLAGS := %s\n' "$$XCPPFLAGS" >>$@
	@printf '_CFLAGS := %s\n' "$$XCFLAGS" >>$@
	@printf '_LDFLAGS := %s\n' "$$XLDFLAGS" >>$@
	@printf '_LDLIBS := %s\n' "$$XLDLIBS" >>$@
	@printf 'NOLIBS := %s\n' "$$XNOLIBS" >>$@
	@test "${OS}-${SAN}" != FreeBSD-y || printf 'POSTLINK = elfctl -e +noaslr $$@\n' >>$@
	@cat ${.ALLSRC} >>$@
	${VCAT} $@
.PHONY: gen/flags.mk

# The short name of the config test
SLUG = ${@:gen/%.mk=%}
# The source file to build
CSRC = build/${SLUG}.c
# The hidden output file name
OUT = ${SLUG:flags/%=gen/flags/.%.out}

${AUTO_FLAGS}::
	@${MKDIR} ${@D}
	@build/flags-if.sh ${CSRC} -o ${OUT} >$@ 2>$@.log; \
	    build/msg-if.sh "[ CC ] ${SLUG}.c" test $$? -eq 0
