#!/bin/sh

# Copyright © Tavian Barnes <tavianator@tavianator.com>
# SPDX-License-Identifier: 0BSD

# Run the compiler and check if it succeeded

set -eux

$XCC $XCPPFLAGS $XCFLAGS $XLDFLAGS "$@" $XLDLIBS -o /dev/null
