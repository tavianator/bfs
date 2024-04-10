#!/usr/bin/env bash

# Copyright © Tavian Barnes <tavianator@tavianator.com>
# SPDX-License-Identifier: 0BSD

# Run the compiler and check if it succeeded

set -eux

$XCC \
    $BFS_CPPFLAGS $XCPPFLAGS ${EXTRA_CPPFLAGS:-} \
    $BFS_CFLAGS $XCFLAGS ${EXTRA_CFLAGS:-} \
    $XLDFLAGS ${EXTRA_LDFLAGS:-} \
    "$@" \
    $XLDLIBS ${EXTRA_LDLIBS:-} $BFS_LDLIBS \
    -o /dev/null
