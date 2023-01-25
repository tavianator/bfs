#!/usr/bin/env bash

# Copyright © Tavian Barnes <tavianator@tavianator.com>
# SPDX-License-Identifier: 0BSD

set -e

L=
COLOR=
ARGS=()
for ARG; do
    case "$ARG" in
        -L)
            L="$ARG"
            ;;
        -color)
            COLOR=y
            ;;
        *)
            ARGS+=("$ARG")
            ;;
    esac
done

LS_COLOR="${BASH_SOURCE%/*}/ls-color.sh"

if [ "$COLOR" ]; then
    find "${ARGS[@]}" -exec "$LS_COLOR" $L {} \;
else
    find "${ARGS[@]}"
fi
