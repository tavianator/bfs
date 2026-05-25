#!/bin/sh

# Copyright © Tavian Barnes <tavianator@tavianator.com>
# SPDX-License-Identifier: 0BSD

# Run the compiler and check if it succeeded.  Usage:
#
#     $ build/cc.sh [-q] path/to/file.c [-flags -Warnings ...]

set -eu

# Without -q, print the executed command for config.log
if [ "$1" = "-q" ]; then
    shift
else
    set -x
fi

$XCC $XCPPFLAGS $XCFLAGS $XLDFLAGS "$@" $XLDLIBS
