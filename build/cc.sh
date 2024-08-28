#!/bin/sh

# Copyright © Tavian Barnes <tavianator@tavianator.com>
# SPDX-License-Identifier: 0BSD

# Run the compiler and check if it succeeded

set -eu

if [ "$1" = "-q" ]; then
    shift
else
    set -x
fi

$XCC $XCPPFLAGS $XCFLAGS $XLDFLAGS "$@" $XLDLIBS
