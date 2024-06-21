#!/bin/sh

# Copyright © Tavian Barnes <tavianator@tavianator.com>
# SPDX-License-Identifier: 0BSD

# pkg-config wrapper with hardcoded fallbacks

set -eu

MODE=
case "${1:-}" in
    --*)
        MODE="$1"
        shift
esac

if [ $# -lt 1 ]; then
    exit
fi

case "$XNOLIBS" in
    y|1)
        exit 1
esac

if [ -z "$MODE" ]; then
    # Check whether the libraries exist at all
    for LIB; do
        # Check ${WITH_$LIB}
        WITH_LIB="WITH_$(printf '%s' "$LIB" | tr 'a-z-' 'A-Z_')"
        eval "WITH=\"\${$WITH_LIB:-}\""
        case "$WITH" in
            y|1) continue ;;
            n|0) exit 1 ;;
        esac

        CFLAGS=$("$0" --cflags "$LIB") || exit 1
        LDFLAGS=$("$0" --ldflags "$LIB") || exit 1
        LDLIBS=$("$0" --ldlibs "$LIB") || exit 1
        build/cc.sh $CFLAGS $LDFLAGS "build/with/$LIB.c" $LDLIBS -o "gen/with/.$LIB.out" || exit 1
    done
fi

# Defer to pkg-config if possible
if command -v "${XPKG_CONFIG:-}" >/dev/null 2>&1; then
    case "$MODE" in
        --cflags)
            "$XPKG_CONFIG" --cflags "$@"
            ;;
        --ldflags)
            "$XPKG_CONFIG" --libs-only-L --libs-only-other "$@"
            ;;
        --ldlibs)
            "$XPKG_CONFIG" --libs-only-l "$@"
            ;;
    esac

    exit
fi

# pkg-config unavailable, emulate it ourselves
CFLAGS=""
LDFLAGS=""
LDLIBS=""

for LIB; do
    case "$LIB" in
        libacl)
            LDLIB=-lacl
            ;;
        libcap)
            LDLIB=-lcap
            ;;
        libselinux)
            LDLIB=-lselinux
            ;;
        liburing)
            LDLIB=-luring
            ;;
        oniguruma)
            LDLIB=-lonig
            ;;
        *)
            printf 'error: Unknown package %s\n' "$LIB" >&2
            exit 1
            ;;
    esac

    LDLIBS="$LDLIBS$LDLIB "
done

case "$MODE" in
    --ldlibs)
        printf '%s\n' "$LDLIBS"
    ;;
esac
