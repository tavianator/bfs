#!/bin/sh

# Copyright © Tavian Barnes <tavianator@tavianator.com>
# SPDX-License-Identifier: 0BSD

# Add flags to a makefile if a build succeeds

set -eu

OLD_FLAGS="$XCC $XCPPFLAGS $XCFLAGS $XLDFLAGS $XLDLIBS"
NEW_FLAGS=$(sed -n '\|^///|{s|^/// ||; s|[^=]*= ||; p;}' "$1")
build/cc.sh "$@" $NEW_FLAGS || exit 1

# De-duplicate against the existing flags
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
