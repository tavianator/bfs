#!/bin/sh

# Copyright © Tavian Barnes <tavianator@tavianator.com>
# SPDX-License-Identifier: 0BSD

# Add flags to a makefile if a build succeeds

set -eu

build/cc.sh "$@" || exit 1

# If the build succeeded, print any lines like
#
#     /// _CFLAGS += -foo
#
# (unless they're already set)
OLD_FLAGS="$XCC $XCPPFLAGS $XCFLAGS $XLDFLAGS $XLDLIBS"

while IFS="" read -r line; do
    case "$line" in
        ///*=*)
            flag="${line#*= }"
            if [ "${OLD_FLAGS#*"$flag"}" = "$OLD_FLAGS" ]; then
                printf '%s\n' "${line#/// }"
            fi
            ;;
    esac
done <"$1"
