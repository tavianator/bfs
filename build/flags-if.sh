#!/bin/sh

# Copyright © Tavian Barnes <tavianator@tavianator.com>
# SPDX-License-Identifier: 0BSD

# Add flags to a makefile if a build succeeds.  Source files can specify their
# own flags with lines like
#
#     /// _CFLAGS += -Wmissing-variable-declarations
#
# which will be added to the makefile on success, or lines like
#
#     /// -Werror
#
# which are just used for the current file.  Lines like
#
#     /// ---
#
# separate groups of flags, to try multiple ways to achieve something, e.g.
#
#     /// CFLAGS += -pthread
#     /// ---
#     /// LDLIBS += -lpthread

set -eu

# Any new flags we're using
FLAGS=""
# Any new makefile lines we're printing
OUTPUT=""

# Check the existing flags so we don't add duplicates
OLD_FLAGS=" $XCPPFLAGS $XCFLAGS $XLDFLAGS $XLDLIBS "

add_flag() {
    case "$1" in
        -std=*)
            # Don't overwrite -std=* flags
            case "$OLD_FLAGS" in
                *\ -std=*)
                    return 1
                    ;;
            esac
            ;;
        *)
            if [ "${OLD_FLAGS#* $1 }" != "$OLD_FLAGS" ]; then
                return 1
            fi
            ;;
    esac

    FLAGS="${FLAGS}${FLAGS:+ }$1"
}

try_cc() {
    build/cc.sh "$@" $FLAGS || return $?
    printf '%s' "$OUTPUT"
    exit
}

while IFS="" read -r line; do
    case "$line" in
        "/// "*)
            line="${line#/// }"
            ;;
        *)
            continue
            ;;
    esac

    case "$line" in
        ---)
            try_cc "$@" || :
            FLAGS=
            OUTPUT=
            ;;
        *=*)
            if add_flag "${line#*= }"; then
                OUTPUT="${OUTPUT}${line}
"
            fi
            ;;
        *)
            add_flag "$line" || :
            ;;
    esac
done <"$1"

try_cc "$@"
