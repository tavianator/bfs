#!/bin/sh

# Copyright © Tavian Barnes <tavianator@tavianator.com>
# SPDX-License-Identifier: 0BSD

# Convert data into a C array like #embed

set -eu

{ cat; printf '\0'; } \
    | od -An -tx1 \
    | sed 's/\([^ ][^ ]*\)/0x\1,/g'
