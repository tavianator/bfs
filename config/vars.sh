#!/usr/bin/env bash

# Copyright © Tavian Barnes <tavianator@tavianator.com>
# SPDX-License-Identifier: 0BSD

# Writes the saved variables to gen/vars.mk

set -eu

print() {
    NAME="$1"
    OP="${2:-:=}"

    if (($# >= 3)); then
        printf '# %s\n' "${3#X}"
        declare -n VAR="$3"
        VALUE="${VAR:-}"
    else
        # Try X$NAME, $NAME, ""
        local -n XVAR="X$NAME"
        local -n VAR="$NAME"
        VALUE="${XVAR:-${VAR:-}}"
    fi

    printf '%s %s %s\n' "$NAME" "$OP" "$VALUE"
}

cond_flags() {
    local -n COND="$1"

    if [[ "${COND:-}" == *y* ]]; then
        print "$2" += "${1}_${2}"
    fi
}

print PREFIX
print MANDIR

print OS
print ARCH

print CC
print INSTALL
print MKDIR
print RM

print CPPFLAGS := BFS_CPPFLAGS
cond_flags TSAN CPPFLAGS
cond_flags LINT CPPFLAGS
cond_flags RELEASE CPPFLAGS
print CPPFLAGS += XCPPFLAGS
print CPPFLAGS += EXTRA_CPPFLAGS

print CFLAGS := BFS_CFLAGS
cond_flags ASAN CFLAGS
cond_flags LSAN CFLAGS
cond_flags MSAN CFLAGS
cond_flags TSAN CFLAGS
cond_flags UBSAN CFLAGS
cond_flags SAN CFLAGS
cond_flags GCOV CFLAGS
cond_flags LINT CFLAGS
cond_flags RELEASE CFLAGS
print CFLAGS += XCFLAGS
print CFLAGS += EXTRA_CFLAGS

print LDFLAGS := XLDFLAGS
print LDFLAGS += EXTRA_LDFLAGS

print LDLIBS := XLDLIBS
print LDLIBS += EXTRA_LDLIBS
print LDLIBS += BFS_LDLIBS

print PKGS

# Disable ASLR on FreeBSD when sanitizers are enabled
case "$XOS-$SAN" in
    FreeBSD-*y*)
        printf 'POSTLINK = elfctl -e +noaslr $$@\n'
        ;;
esac
