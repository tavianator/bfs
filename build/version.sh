#!/bin/sh

# Copyright © Tavian Barnes <tavianator@tavianator.com>
# SPDX-License-Identifier: 0BSD

# Print the version number

set -eu

DIR="$(dirname -- "$0")/.."

if [ "${VERSION-}" ]; then
    printf '%s' "$VERSION"
elif [ -e "$DIR/.git" ] && command -v git >/dev/null 2>&1; then
    git -C "$DIR" describe --always --dirty
else
    echo "4.0.6"
fi
