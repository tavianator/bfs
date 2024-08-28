#!/bin/sh

# Copyright © Tavian Barnes <tavianator@tavianator.com>
# SPDX-License-Identifier: 0BSD

# Run the compiler and check if it succeeded.  Usage:
#
#     $ build/cc.sh [-q] path/to/file.c [-flags -Warnings ...]

set -eu

QUIET=
if [ "$1" = "-q" ]; then
    QUIET=y
    shift
fi

# Source files can specify their own flags with lines like
#
#     /// _CFLAGS += -Wmissing-variable-declarations
#
# which will be added to the makefile on success, or lines like
#
#     /// -Werror
#
# which are just used for the current file.
EXTRA_FLAGS=$(sed -n '\|^///|{s|^/// ||; s|[^=]*= ||; p;}' "$1")

# Without -q, print the executed command for config.log
if [ -z "$QUIET" ]; then
    set -x
fi

$XCC $XCPPFLAGS $XCFLAGS $XLDFLAGS "$@" $EXTRA_FLAGS $XLDLIBS
