#!/bin/sh

# Copyright © Tavian Barnes <tavianator@tavianator.com>
# SPDX-License-Identifier: 0BSD

# Output a C preprocessor definition based on whether a C source file could be
# compiled successfully

set -eu

SLUG="${1#config/}"
SLUG="${SLUG%.c}"
MACRO="BFS_HAS_$(printf '%s' "$SLUG" | tr 'a-z-' 'A-Z_')"

if config/cc.sh "$1"; then
    printf '#define %s true\n' "$MACRO"
else
    printf '#define %s false\n' "$MACRO"
fi
