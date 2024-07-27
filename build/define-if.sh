#!/bin/sh

# Copyright © Tavian Barnes <tavianator@tavianator.com>
# SPDX-License-Identifier: 0BSD

# Output a C preprocessor definition based on whether a command succeeds

set -eu

MACRO=$(printf 'BFS_%s' "$1" | tr '/a-z-' '_A-Z_')
shift

if "$@"; then
    printf '#define %s true\n' "$MACRO"
else
    printf '#define %s false\n' "$MACRO"
    exit 1
fi
