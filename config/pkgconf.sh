#!/usr/bin/env bash

# Copyright © Tavian Barnes <tavianator@tavianator.com>
# SPDX-License-Identifier: 0BSD

# pkg-config wrapper with hardcoded fallbacks

set -eu

MODE=
if [[ "$1" == --* ]]; then
    MODE="$1"
    shift
fi

if command -v "${PKG_CONFIG:-}" &>/dev/null; then
    case "$MODE" in
        --cflags)
            "$PKG_CONFIG" --cflags "$@"
            ;;
        --ldflags)
            "$PKG_CONFIG" --libs-only-L --libs-only-other "$@"
            ;;
        --ldlibs)
            "$PKG_CONFIG" --libs-only-l "$@"
            ;;
        "")
            "$PKG_CONFIG" "$@"
            ;;
    esac
else
    for lib; do
        case "$lib" in
            libacl)
                LDLIB=-lacl
                ;;
            libcap)
                LDLIB=-lcap
                ;;
            liburing)
                LDLIB=-luring
                ;;
            oniguruma)
                LDLIB=-lonig
                ;;
            *)
                printf 'error: Unknown package %s\n' "$lib" >&2
                exit 1
        esac

        case "$MODE" in
            --ldlibs)
                printf ' %s' "$LDLIB"
                ;;
            "")
                config/cc.sh "config/$lib.c" "$LDLIB" || exit $?
                ;;
        esac
    done

    if [ "$MODE" = "--ldlibs" ]; then
        printf '\n'
    fi
fi
