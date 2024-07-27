#!/bin/sh

# Copyright © Tavian Barnes <tavianator@tavianator.com>
# SPDX-License-Identifier: 0BSD

# Add flags to a makefile if a build succeeds

set -eu

FLAGS=$(sed -n '\|^///|{s|^/// ||; s|[^=]*= ||; p}' "$1")

if build/cc.sh "$@" $FLAGS; then
    sed -n 's|^/// \(.*=.*\)|\1|p' "$1"
else
    exit 1
fi
