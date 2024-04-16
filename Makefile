# Copyright © Tavian Barnes <tavianator@tavianator.com>
# SPDX-License-Identifier: 0BSD

# This Makefile implements the configuration and build steps for bfs.  It is
# portable to both GNU make and most BSD make implementations.  To build bfs,
# run
#
#     $ make config
#     $ make

# Utilities and GNU/BSD portability
include config/prelude.mk

# The default build target
default: bfs
.PHONY: default

# Include the generated build config, if it exists
-include ${CONFIG}

## Configuration phase (`make config`)

# The configuration goal itself
config::
	@+${MAKE} -sf config/config.mk

# bfs used to have flag-like targets (`make release`, `make asan ubsan`, etc.).
# Direct users to the new configuration system.
asan lsan msan tsan ubsan gcov lint release::
	@printf 'error: `%s %s` is no longer supported. ' "${MAKE}" $@ >&2
	@printf 'Use `%s config %s=y` instead.\n' "${MAKE}" $$(echo $@ | tr '[a-z]' '[A-Z]') >&2
	@false

# Print an error if `make` is run before `make config`
${CONFIG}::
	@if ! [ -e $@ ]; then \
	    printf 'error: You must run `%s config` before `%s`.\n' "${MAKE}" "${MAKE}" >&2; \
	    false; \
	fi

## Build phase (`make`)

# The main binary
bfs: ${BIN}/bfs
.PHONY: bfs

# All binaries
BINS := \
    ${BIN}/bfs \
    ${BIN}/tests/mksock \
    ${BIN}/tests/units \
    ${BIN}/tests/xspawnee \
    ${BIN}/tests/xtouch

all: ${BINS}
.PHONY: all

# Group relevant flags together
ALL_CFLAGS = ${CPPFLAGS} ${CFLAGS} ${DEPFLAGS}
ALL_LDFLAGS = ${CFLAGS} ${LDFLAGS}

# The main binary
${BIN}/bfs: ${LIBBFS} ${OBJ}/src/main.o

${BINS}:
	@${MKDIR} ${@D}
	+${MSG} "[ LD ] $@" ${CC} ${ALL_LDFLAGS} ${.ALLSRC} ${LDLIBS} -o $@
	${POSTLINK}

# Get the .c file for a .o file
CSRC = ${@:${OBJ}/%.o=%.c}

# Depend on ${CONFIG} to make sure `make config` runs first, and to rebuild when
# the configuration changes
${OBJS}: ${CONFIG}
	@${MKDIR} ${@D}
	${MSG} "[ CC ] ${CSRC}" ${CC} ${ALL_CFLAGS} -c ${CSRC} -o $@

# Save the version number to this file, but only update VERSION if it changes
${GEN}/NEWVERSION::
	@${MKDIR} ${@D}
	@if [ "$$VERSION" ]; then \
	    printf '%s\n' "$$VERSION"; \
	elif test -d .git && command -v git >/dev/null 2>&1; then \
	    git describe --always --dirty; \
	else \
	    echo "3.1.3"; \
	fi >$@

${GEN}/VERSION: ${GEN}/NEWVERSION
	@test -e $@ && cmp -s $@ ${.ALLSRC} && rm ${.ALLSRC} || mv ${.ALLSRC} $@

# Rebuild version.c whenever the version number changes
${OBJ}/src/version.o: ${GEN}/VERSION
${OBJ}/src/version.o: CPPFLAGS := ${CPPFLAGS} -DBFS_VERSION='"$$(cat ${GEN}/VERSION)"'

## Test phase (`make check`)

# Unit test binaries
UTEST_BINS := \
    ${BIN}/tests/units \
    ${BIN}/tests/xspawnee

# Integration test binaries
ITEST_BINS := \
    ${BIN}/tests/mksock \
    ${BIN}/tests/xtouch

# Build (but don't run) test binaries
tests: ${UTEST_BINS} ${ITEST_BINS}
.PHONY: tests

# Run all the tests
check: unit-tests integration-tests
.PHONY: check

# Run the unit tests
unit-tests: ${UTEST_BINS}
	${MSG} "[TEST] tests/units" ${BIN}/tests/units
.PHONY: unit-tests

${BIN}/tests/units: \
    ${UNIT_OBJS} \
    ${LIBBFS}

${BIN}/tests/xspawnee: \
    ${OBJ}/tests/xspawnee.o

# The different flag combinations we check
INTEGRATIONS := default dfs ids eds j1 j2 j3 s
INTEGRATION_TESTS := ${INTEGRATIONS:%=check-%}

# Check just `bfs`
check-default: ${BIN}/bfs ${ITEST_BINS}
	+${MSG} "[TEST] bfs" \
	    ./tests/tests.sh --make="${MAKE}" --bfs="${BIN}/bfs" ${TEST_FLAGS}

# Check the different search strategies
check-dfs check-ids check-eds: ${BIN}/bfs ${ITEST_BINS}
	+${MSG} "[TEST] bfs -S ${@:check-%=%}" \
	    ./tests/tests.sh --make="${MAKE}" --bfs="${BIN}/bfs -S ${@:check-%=%}" ${TEST_FLAGS}

# Check various flags
check-j1 check-j2 check-j3 check-s: ${BIN}/bfs ${ITEST_BINS}
	+${MSG} "[TEST] bfs -${@:check-%=%}" \
	    ./tests/tests.sh --make="${MAKE}" --bfs="${BIN}/bfs -${@:check-%=%}" ${TEST_FLAGS}

# Run the integration tests
integration-tests: ${INTEGRATION_TESTS}
.PHONY: integration-tests

${BIN}/tests/mksock: \
    ${OBJ}/tests/mksock.o \
    ${LIBBFS}

${BIN}/tests/xtouch: \
    ${OBJ}/tests/xtouch.o \
    ${LIBBFS}

# `make distcheck` configurations
DISTCHECKS := \
    distcheck-asan \
    distcheck-msan \
    distcheck-tsan \
    distcheck-m32 \
    distcheck-release

# Test multiple configurations
distcheck:
	@+${MAKE} -s distcheck-asan
	@+test "$$(uname)" = Darwin || ${MAKE} -s distcheck-msan
	@+${MAKE} -s distcheck-tsan
	@+test "$$(uname)-$$(uname -m)" != Linux-x86_64 || ${MAKE} -s distcheck-m32
	@+${MAKE} -s distcheck-release
.PHONY: distcheck

# Per-distcheck configuration
DISTCHECK_CONFIG_asan := ASAN=y UBSAN=y
DISTCHECK_CONFIG_msan := MSAN=y UBSAN=y CC=clang
DISTCHECK_CONFIG_tsan := TSAN=y UBSAN=y CC=clang
DISTCHECK_CONFIG_m32 := EXTRA_CFLAGS="-m32" PKG_CONFIG_LIBDIR=/usr/lib32/pkgconfig
DISTCHECK_CONFIG_release := RELEASE=y

${DISTCHECKS}::
	+${MAKE} -rs BUILDDIR=${BUILDDIR}/$@ config ${DISTCHECK_CONFIG_${@:distcheck-%=%}}
	+${MAKE} -s BUILDDIR=${BUILDDIR}/$@ check TEST_FLAGS="--sudo --verbose=skipped"

## Packaging (`make install`)

DEST_PREFIX := ${DESTDIR}${PREFIX}
DEST_MANDIR := ${DESTDIR}${MANDIR}

install::
	${Q}${MKDIR} ${DEST_PREFIX}/bin
	${MSG} "[INSTALL] bin/bfs" \
	    ${INSTALL} -m755 ${BIN}/bfs ${DEST_PREFIX}/bin/bfs
	${Q}${MKDIR} ${DEST_MANDIR}/man1
	${MSG} "[INSTALL] man/man1/bfs.1" \
	    ${INSTALL} -m644 docs/bfs.1 ${DEST_MANDIR}/man1/bfs.1
	${Q}${MKDIR} ${DEST_PREFIX}/share/bash-completion/completions
	${MSG} "[INSTALL] completions/bfs.bash" \
	    ${INSTALL} -m644 completions/bfs.bash ${DEST_PREFIX}/share/bash-completion/completions/bfs
	${Q}${MKDIR} ${DEST_PREFIX}/share/zsh/site-functions
	${MSG} "[INSTALL] completions/bfs.zsh" \
	    ${INSTALL} -m644 completions/bfs.zsh ${DEST_PREFIX}/share/zsh/site-functions/_bfs
	${Q}${MKDIR} ${DEST_PREFIX}/share/fish/vendor_completions.d
	${MSG} "[INSTALL] completions/bfs.fish" \
	    ${INSTALL} -m644 completions/bfs.fish ${DEST_PREFIX}/share/fish/vendor_completions.d/bfs.fish

uninstall::
	${RM} ${DEST_PREFIX}/share/bash-completion/completions/bfs
	${RM} ${DEST_PREFIX}/share/zsh/site-functions/_bfs
	${RM} ${DEST_PREFIX}/share/fish/vendor_completions.d/bfs.fish
	${RM} ${DEST_MANDIR}/man1/bfs.1
	${RM} ${DEST_PREFIX}/bin/bfs

# Check that `make install` works and `make uninstall` removes everything
check-install::
	+${MAKE} install DESTDIR=${BUILDDIR}/pkg
	+${MAKE} uninstall DESTDIR=${BUILDDIR}/pkg
	${BIN}/bfs ${BUILDDIR}/pkg -not -type d -print -exit 1
	${RM} -r ${BUILDDIR}/pkg

## Cleanup (`make clean`)

# Clean all build products
clean::
	${MSG} "[ RM ] bin obj" \
	    ${RM} -r ${BIN} ${OBJ}

# Clean everything, including generated files
distclean: clean
	${MSG} "[ RM ] gen" \
	    ${RM} -r ${GEN} ${DISTCHECKS}
.PHONY: distclean
