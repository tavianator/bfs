#!/bin/sh

# Copyright © Tavian Barnes <tavianator@tavianator.com>
# SPDX-License-Identifier: 0BSD

# Print a message from a makefile:
#
#     $ make -s
#     $ make
#     [ CC ] src/main.c
#     $ make V=1
#     cc -Isrc -Igen -D...

set -eu

# Get the $MAKEFLAGS from the top-level make invocation
MFLAGS="${XMAKEFLAGS-${MAKEFLAGS-}}"

# Check if make should be quiet (make -s)
is_quiet() {
    # GNU make puts single-letter flags in the first word of $MAKEFLAGS,
    # without a leading dash
    case "${MFLAGS%% *}" in
        -*) : ;;
        *s*) return 0 ;;
    esac

    # BSD make puts each flag separately like -r -s -j 48
    for flag in $MFLAGS; do
        case "$flag" in
            # Ignore things like --jobserver-auth
            --*) continue ;;
            # Skip variable assignments
            *=*) break ;;
            -*s*) return 0 ;;
        esac
    done

    return 1
}

# Check if make should be loud (make V=1)
is_loud() {
    test "$XV"
}

MSG="$1"
shift

if ! is_quiet && ! is_loud; then
    printf '%s\n' "$MSG"
fi

if [ $# -eq 0 ]; then
    exit
fi

if is_loud; then
    printf '%s\n' "$*"
fi

exec "$@"
