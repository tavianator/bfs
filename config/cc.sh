#!/bin/sh

# Copyright © Tavian Barnes <tavianator@tavianator.com>
# SPDX-License-Identifier: 0BSD

# Run the compiler and check if it succeeded

set -eu

TMP=$(mktemp)
trap 'rm -f "$TMP"' EXIT

(
    set -x
    $XCC $XCPPFLAGS $XCFLAGS $XLDFLAGS "$@" $XLDLIBS -o "$TMP"
)
