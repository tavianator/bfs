#!/usr/bin/env bash

# Copyright © Tavian Barnes <tavianator@tavianator.com>
# SPDX-License-Identifier: 0BSD

# Run the compiler and check if it succeeded

set -eux

$CC $CPPFLAGS $CFLAGS $LDFLAGS "$@" $LDLIBS -o /dev/null
