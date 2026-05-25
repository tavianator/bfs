#!/bin/sh

# Copyright © Tavian Barnes <tavianator@tavianator.com>
# SPDX-License-Identifier: 0BSD

# Print a success/failure indicator from a makefile:
#
#     $ ./configure
#     [ CC ] with/liburing.c                 ✘
#     [ CC ] with/oniguruma.c                ✔

set -eu

MSG="$1"
shift

if [ -z "${NO_COLOR:-}" ] && [ -t 1 ]; then
    Y='\033[1;32m✔\033[0m'
    N='\033[1;31m✘\033[0m'
else
    Y='✔'
    N='✘'
fi

if "$@"; then
    YN="$Y"
else
    YN="$N"
fi

build/msg.sh "$(printf "%-37s  $YN" "$MSG")"
