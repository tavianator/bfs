#!/usr/bin/env bash

# Copyright © Tavian Barnes <tavianator@tavianator.com>
# SPDX-License-Identifier: 0BSD

# pkg-config wrapper with hardcoded fallbacks

set -eu

MODE=
if [[ "${1:-}" == --* ]]; then
    MODE="$1"
    shift
fi

if (($# < 1)); then
    exit
fi

if [[ "$NOLIBS" == *y* ]]; then
    exit 1
fi

if command -v "${PKG_CONFIG:-}" &>/dev/null; then
    case "$MODE" in
        "")
            "$PKG_CONFIG" "$@"
            ;;
        --cflags)
            OUT=$("$PKG_CONFIG" --cflags "$@")
            if [ "$OUT" ]; then
                printf 'CFLAGS += %s\n' "$OUT"
            fi
            ;;
        --ldflags)
            OUT=$("$PKG_CONFIG" --libs-only-L --libs-only-other "$@")
            if [ "$OUT" ]; then
                printf 'LDFLAGS += %s\n' "$OUT"
            fi
            ;;
        --ldlibs)
            OUT=$("$PKG_CONFIG" --libs-only-l "$@")
            if [ "$OUT" ]; then
                printf 'LDLIBS := %s ${LDLIBS}\n' "$OUT"
            fi
            ;;
    esac
else
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

        case "$MODE" in
            "")
                config/cc.sh "config/$LIB.c" "$LDLIB" || exit $?
                ;;
            --ldlibs)
                LDLIBS="$LDLIBS $LDLIB"
                ;;
        esac
    done

    if [ "$MODE" = "--ldlibs" ] && [ "$LDLIBS" ]; then
        printf 'LDLIBS :=%s ${LDLIBS}\n' "$LDLIBS"
    fi
fi
