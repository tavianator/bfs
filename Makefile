# Copyright © Tavian Barnes <tavianator@tavianator.com>
# SPDX-License-Identifier: 0BSD

# To build bfs, run
#
#     $ ./configure
#     $ make

# Utilities and GNU/BSD portability
include build/prelude.mk

# The default build target
default: bfs
.PHONY: default

# Include the generated build config, if it exists
-include gen/config.mk

## Configuration phase (`./configure`)

# bfs used to have flag-like targets (`make release`, `make asan ubsan`, etc.).
# Direct users to the new configuration system.
asan lsan msan tsan ubsan gcov lint release::
	@printf 'error: `%s %s` is no longer supported. Use `./configure --enable-%s` instead.\n' \
	    "${MAKE}" $@ $@ >&2
	@false

# Print an error if `make` is run before `./configure`
gen/config.mk::
	if ! [ -e $@ ]; then \
	    printf 'error: You must run `./configure` before `%s`.\n' "${MAKE}" >&2; \
	    false; \
	fi
.SILENT: gen/config.mk

## Build phase (`make`)

# The main binary
bfs: bin/bfs
.PHONY: bfs

# All binaries
BINS := \
    bin/bfs \
    bin/tests/mksock \
    bin/tests/units \
    bin/tests/xspawnee \
    bin/tests/xtouch

all: ${BINS}
.PHONY: all

# All object files except the entry point
LIBBFS := \
    obj/src/alloc.o \
    obj/src/bar.o \
    obj/src/bfstd.o \
    obj/src/bftw.o \
    obj/src/color.o \
    obj/src/ctx.o \
    obj/src/diag.o \
    obj/src/dir.o \
    obj/src/dstring.o \
    obj/src/eval.o \
    obj/src/exec.o \
    obj/src/expr.o \
    obj/src/fsade.o \
    obj/src/ioq.o \
    obj/src/mtab.o \
    obj/src/opt.o \
    obj/src/parse.o \
    obj/src/printf.o \
    obj/src/pwcache.o \
    obj/src/sighook.o \
    obj/src/stat.o \
    obj/src/thread.o \
    obj/src/trie.o \
    obj/src/typo.o \
    obj/src/version.o \
    obj/src/xregex.o \
    obj/src/xspawn.o \
    obj/src/xtime.o

# All object files
OBJS := ${LIBBFS}

# The main binary
bin/bfs: obj/src/main.o ${LIBBFS}
OBJS += obj/src/main.o

${BINS}:
	@${MKDIR} ${@D}
	+${MSG} "[ LD ] $@" ${CC} ${_CFLAGS} ${_LDFLAGS} ${.ALLSRC} ${_LDLIBS} -o $@
	${POSTLINK}

# Get the .c file for a .o file
CSRC = ${@:obj/%.o=%.c}

# Save the version number to this file, but only update version.c if it changes
gen/version.i.new::
	${MKDIR} ${@D}
	build/version.sh | tr -d '\n' | build/embed.sh >$@
.SILENT: gen/version.i.new

gen/version.i: gen/version.i.new
	test -e $@ && cmp -s $@ ${.ALLSRC} && ${RM} ${.ALLSRC} || mv ${.ALLSRC} $@
.SILENT: gen/version.i

obj/src/version.o: gen/version.i

## Test phase (`make check`)

# Unit test binaries
UTEST_BINS := \
    bin/tests/units \
    bin/tests/xspawnee

# Integration test binaries
ITEST_BINS := \
    bin/tests/mksock \
    bin/tests/xtouch

# Build (but don't run) test binaries
tests: ${UTEST_BINS} ${ITEST_BINS}
.PHONY: tests

# Run all the tests
check: unit-tests integration-tests
.PHONY: check

# Run the unit tests
unit-tests: ${UTEST_BINS}
	${MSG} "[TEST] tests/units" bin/tests/units
.PHONY: unit-tests

# Unit test objects
UNIT_OBJS := \
    obj/tests/alloc.o \
    obj/tests/bfstd.o \
    obj/tests/bit.o \
    obj/tests/ioq.o \
    obj/tests/list.o \
    obj/tests/main.o \
    obj/tests/sighook.o \
    obj/tests/trie.o \
    obj/tests/xspawn.o \
    obj/tests/xtime.o

bin/tests/units: ${UNIT_OBJS} ${LIBBFS}
OBJS += ${UNIT_OBJS}

bin/tests/xspawnee: obj/tests/xspawnee.o
OBJS += obj/tests/xspawnee.o

# The different flag combinations we check
INTEGRATIONS := default dfs ids eds j1 j2 j3 s
INTEGRATION_TESTS := ${INTEGRATIONS:%=check-%}

# Check just `bfs`
check-default: bin/bfs ${ITEST_BINS}
	+${MSG} "[TEST] bfs" \
	    ./tests/tests.sh --make="${MAKE}" --bfs="bin/bfs" ${TEST_FLAGS}

# Check the different search strategies
check-dfs check-ids check-eds: bin/bfs ${ITEST_BINS}
	+${MSG} "[TEST] bfs -S ${@:check-%=%}" \
	    ./tests/tests.sh --make="${MAKE}" --bfs="bin/bfs -S ${@:check-%=%}" ${TEST_FLAGS}

# Check various flags
check-j1 check-j2 check-j3 check-s: bin/bfs ${ITEST_BINS}
	+${MSG} "[TEST] bfs -${@:check-%=%}" \
	    ./tests/tests.sh --make="${MAKE}" --bfs="bin/bfs -${@:check-%=%}" ${TEST_FLAGS}

# Run the integration tests
integration-tests: ${INTEGRATION_TESTS}
.PHONY: integration-tests

bin/tests/mksock: obj/tests/mksock.o ${LIBBFS}
OBJS += obj/tests/mksock.o

bin/tests/xtouch: obj/tests/xtouch.o ${LIBBFS}
OBJS += obj/tests/xtouch.o

# `make distcheck` configurations
DISTCHECKS := \
    distcheck-asan \
    distcheck-msan \
    distcheck-tsan \
    distcheck-m32 \
    distcheck-release

# Test multiple configurations
distcheck:
	@+${MAKE} distcheck-asan
	@+test "$$(uname)" = Darwin || ${MAKE} distcheck-msan
	@+test "$$(uname)" = FreeBSD || ${MAKE} distcheck-tsan
	@+test "$$(uname)-$$(uname -m)" != Linux-x86_64 || ${MAKE} distcheck-m32
	@+${MAKE} distcheck-release
	@+${MAKE} -C distcheck-release check-install
	@+test "$$(uname)" != Linux || ${MAKE} check-man
.PHONY: distcheck

# Per-distcheck configuration
DISTCHECK_CONFIG_asan := --enable-asan --enable-ubsan
DISTCHECK_CONFIG_msan := --enable-msan --enable-ubsan CC=clang
DISTCHECK_CONFIG_tsan := --enable-tsan --enable-ubsan CC=clang
DISTCHECK_CONFIG_m32 := EXTRA_CFLAGS="-m32" PKG_CONFIG_LIBDIR=/usr/lib32/pkgconfig
DISTCHECK_CONFIG_release := --enable-release

${DISTCHECKS}::
	@${MKDIR} $@
	@test "$${GITHUB_ACTIONS-}" != true || printf '::group::%s\n' $@
	@+cd $@ \
	    && ../configure MAKE="${MAKE}" ${DISTCHECK_CONFIG_${@:distcheck-%=%}} \
	    && ${MAKE} check TEST_FLAGS="--sudo --verbose=skipped"
	@test "$${GITHUB_ACTIONS-}" != true || printf '::endgroup::\n'

## Automatic dependency tracking

# Rebuild when the configuration changes
${OBJS}: gen/config.mk
	@${MKDIR} ${@D}
	${MSG} "[ CC ] ${CSRC}" ${CC} ${_CPPFLAGS} ${_CFLAGS} -c ${CSRC} -o $@

# Include any generated dependency files
-include ${OBJS:.o=.d}

## Packaging (`make install`)

DEST_PREFIX := ${DESTDIR}${PREFIX}
DEST_MANDIR := ${DESTDIR}${MANDIR}

install::
	${Q}${MKDIR} ${DEST_PREFIX}/bin
	${MSG} "[INST] bin/bfs" \
	    ${INSTALL} -m755 bin/bfs ${DEST_PREFIX}/bin/bfs
	${Q}${MKDIR} ${DEST_MANDIR}/man1
	${MSG} "[INST] man/man1/bfs.1" \
	    ${INSTALL} -m644 docs/bfs.1 ${DEST_MANDIR}/man1/bfs.1
	${Q}${MKDIR} ${DEST_PREFIX}/share/bash-completion/completions
	${MSG} "[INST] completions/bfs.bash" \
	    ${INSTALL} -m644 completions/bfs.bash ${DEST_PREFIX}/share/bash-completion/completions/bfs
	${Q}${MKDIR} ${DEST_PREFIX}/share/zsh/site-functions
	${MSG} "[INST] completions/bfs.zsh" \
	    ${INSTALL} -m644 completions/bfs.zsh ${DEST_PREFIX}/share/zsh/site-functions/_bfs
	${Q}${MKDIR} ${DEST_PREFIX}/share/fish/vendor_completions.d
	${MSG} "[INST] completions/bfs.fish" \
	    ${INSTALL} -m644 completions/bfs.fish ${DEST_PREFIX}/share/fish/vendor_completions.d/bfs.fish

uninstall::
	${MSG} "[ RM ] completions/bfs.bash" \
	    ${RM} ${DEST_PREFIX}/share/bash-completion/completions/bfs
	${MSG} "[ RM ] completions/bfs.zsh" \
	    ${RM} ${DEST_PREFIX}/share/zsh/site-functions/_bfs
	${MSG} "[ RM ] completions/bfs.fish" \
	    ${RM} ${DEST_PREFIX}/share/fish/vendor_completions.d/bfs.fish
	${MSG} "[ RM ] man/man1/bfs.1" \
	    ${RM} ${DEST_MANDIR}/man1/bfs.1
	${MSG} "[ RM ] bin/bfs" \
	    ${RM} ${DEST_PREFIX}/bin/bfs

# Check that `make install` works and `make uninstall` removes everything
check-install::
	+${MAKE} install DESTDIR=pkg
	+${MAKE} uninstall DESTDIR=pkg
	bin/bfs pkg -not -type d -print -exit 1
	${RM} -r pkg

# Check man page markup
check-man::
	${MSG} "[LINT] docs/bfs.1"
	${Q}groff -man -rCHECKSTYLE=3 -ww -b -z docs/bfs.1
	${Q}mandoc -Tlint -Wwarning docs/bfs.1

## Cleanup (`make clean`)

# Clean all build products
clean::
	${MSG} "[ RM ] bin obj" \
	    ${RM} -r bin obj

# Clean everything, including generated files
distclean: clean
	${MSG} "[ RM ] gen distcheck-*" \
	    ${RM} -r gen ${DISTCHECKS}
.PHONY: distclean
