# Copyright © Tavian Barnes <tavianator@tavianator.com>
# SPDX-License-Identifier: 0BSD

# Makefile that generates gen/{early,late}.mk

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

include build/exports.mk

# Saves the internal flags
gen/early.mk::
	${MSG} "[ GEN] $@"
	@printf '# %s\n' "$@" >$@
	@printf '_CPPFLAGS := %s\n' "$$XCPPFLAGS" >>$@
	@printf '_CFLAGS := %s\n' "$$XCFLAGS" >>$@
	@printf '_LDFLAGS := %s\n' "$$XLDFLAGS" >>$@
	@printf '_LDLIBS := %s\n' "$$XLDLIBS" >>$@
	@printf 'NOLIBS := %s\n' "$$XNOLIBS" >>$@
	@test "${OS}-${SAN}" != FreeBSD-y || printf 'POSTLINK = elfctl -e +noaslr $$@\n' >>$@
	${VCAT} $@

# Save explicit flags from ./configure separately so they can override the rest
gen/late.mk::
	${MSG} "[ GEN] $@"
	@printf '# %s\n' "$@" >$@
	@printf '_CPPFLAGS += %s\n' "$$CONF_CPPFLAGS" >>$@
	@printf '_CFLAGS += %s\n' "$$CONF_CFLAGS" >>$@
	@printf '_LDFLAGS += %s\n' "$$CONF_LDFLAGS" >>$@
	@printf '_LDLIBS := %s $${_LDLIBS}\n' "$$CONF_LDLIBS" >>$@
	${VCAT} $@
