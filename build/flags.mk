# Copyright © Tavian Barnes <tavianator@tavianator.com>
# SPDX-License-Identifier: 0BSD

# Makefile that generates gen/flags.mk

include build/prelude.mk
include gen/vars.mk

# Internal flags
_CPPFLAGS := -Isrc -Igen -include src/prelude.h
_CFLAGS :=
_LDFLAGS :=
_LDLIBS :=

# Platform-specific system libraries
LDLIBS,DragonFly := -lposix1e
LDLIBS,FreeBSD := -lrt
LDLIBS,Linux := -lrt
LDLIBS,NetBSD := -lutil
LDLIBS,QNX := -lregex -lsocket
LDLIBS,SunOS := -lsec -lsocket -lnsl
_LDLIBS += ${LDLIBS,${OS}}

# Build profiles
_ASAN := ${TRUTHY,${ASAN}}
_LSAN := ${TRUTHY,${LSAN}}
_MSAN := ${TRUTHY,${MSAN}}
_TSAN := ${TRUTHY,${TSAN}}
_TYSAN := ${TRUTHY,${TYSAN}}
_UBSAN := ${TRUTHY,${UBSAN}}
_GCOV := ${TRUTHY,${GCOV}}
_LINT := ${TRUTHY,${LINT}}
_RELEASE := ${TRUTHY,${RELEASE}}

LTO ?= ${RELEASE}
_LTO := ${TRUTHY,${LTO}}

ASAN_CFLAGS,y := -fsanitize=address
LSAN_CFLAGS,y := -fsanitize=leak
MSAN_CFLAGS,y := -fsanitize=memory -fsanitize-memory-track-origins
TSAN_CFLAGS,y := -fsanitize=thread
TYSAN_CFLAGS,y := -fsanitize=type
UBSAN_CFLAGS,y := -fsanitize=undefined

_CFLAGS += ${ASAN_CFLAGS,${_ASAN}}
_CFLAGS += ${LSAN_CFLAGS,${_LSAN}}
_CFLAGS += ${MSAN_CFLAGS,${_MSAN}}
_CFLAGS += ${TSAN_CFLAGS,${_TSAN}}
_CFLAGS += ${TYSAN_CFLAGS,${_TYSAN}}
_CFLAGS += ${UBSAN_CFLAGS,${_UBSAN}}

SAN_CFLAGS,y := -fno-sanitize-recover=all
INSANE := ${NOT,${_ASAN}${_LSAN}${_MSAN}${_TSAN}${_TYSAN}${_UBSAN}}
SAN := ${NOT,${INSANE}}
_CFLAGS += ${SAN_CFLAGS,${SAN}}

# MSan, TSan, and TySan need all code to be instrumented
YESLIBS := ${NOT,${_MSAN}${_TSAN}${_TYSAN}}
NOLIBS ?= ${NOT,${YESLIBS}}

GCOV_CFLAGS,y := --coverage
_CFLAGS += ${GCOV_CFLAGS,${_GCOV}}

LINT_CPPFLAGS,y := -D_FORTIFY_SOURCE=3 -DBFS_LINT
LINT_CFLAGS,y := -Werror -O2

_CPPFLAGS += ${LINT_CPPFLAGS,${_LINT}}
_CFLAGS += ${LINT_CFLAGS,${_LINT}}

RELEASE_CPPFLAGS,y := -DNDEBUG
RELEASE_CFLAGS,y := -O3

_CPPFLAGS += ${RELEASE_CPPFLAGS,${_RELEASE}}
_CFLAGS += ${RELEASE_CFLAGS,${_RELEASE}}

LTO_CFLAGS,y := -flto=auto
_CFLAGS += ${LTO_CFLAGS,${_LTO}}

# Configurable flags
CFLAGS ?= -g -Wall

# Add the configurable flags last so they can override ours
_CPPFLAGS += ${CPPFLAGS} ${EXTRA_CPPFLAGS}
_CFLAGS += ${CFLAGS} ${EXTRA_CFLAGS}
_LDFLAGS += ${LDFLAGS} ${EXTRA_LDFLAGS}
# (except LDLIBS, as earlier libs override later ones)
_LDLIBS := ${LDLIBS} ${EXTRA_LDLIBS} ${_LDLIBS}

include build/exports.mk

# Conditionally-supported flags
AUTO_FLAGS := \
    gen/flags/std.mk \
    gen/flags/bind-now.mk \
    gen/flags/deps.mk \
    gen/flags/pthread.mk \
    gen/flags/Wformat.mk \
    gen/flags/Wimplicit-fallthrough.mk \
    gen/flags/Wimplicit.mk \
    gen/flags/Wmissing-decls.mk \
    gen/flags/Wmissing-var-decls.mk \
    gen/flags/Wshadow.mk \
    gen/flags/Wsign-compare.mk \
    gen/flags/Wstrict-prototypes.mk \
    gen/flags/Wundef-prefix.mk

gen/flags.mk: ${AUTO_FLAGS}
	${MSG} "[ GEN] $@"
	@printf '# %s\n' "$@" >$@
	@printf '_CPPFLAGS := %s\n' "$$XCPPFLAGS" >>$@
	@printf '_CFLAGS := %s\n' "$$XCFLAGS" >>$@
	@printf '_LDFLAGS := %s\n' "$$XLDFLAGS" >>$@
	@printf '_LDLIBS := %s\n' "$$XLDLIBS" >>$@
	@printf 'NOLIBS := %s\n' "$$XNOLIBS" >>$@
	@test "${OS}-${SAN}" != FreeBSD-y || printf 'POSTLINK = elfctl -e +noaslr $$@\n' >>$@
	@cat $^ >>$@
	@cat ${^:%=%.log} >gen/flags.log
	${VCAT} $@
.PHONY: gen/flags.mk

# Check that the C compiler works at all
cc::
	@build/cc.sh -q build/empty.c -o gen/.cc.out; \
	    ret=$$?; \
	    build/msg-if.sh "[ CC ] build/empty.c" test $$ret -eq 0; \
	    exit $$ret

# The short name of the config test
SLUG = ${@:gen/%.mk=%}
# The source file to build
CSRC = build/${SLUG}.c
# The hidden output file name
OUT = ${SLUG:flags/%=gen/flags/.%.out}

${AUTO_FLAGS}: cc
	@${MKDIR} ${@D}
	@build/flags-if.sh ${CSRC} -o ${OUT} >$@ 2>$@.log; \
	    build/msg-if.sh "[ CC ] ${SLUG}.c" test $$? -eq 0
.PHONY: ${AUTO_FLAGS}
