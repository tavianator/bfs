#!/bin/sh

# Copyright © Tavian Barnes <tavianator@tavianator.com>
# SPDX-License-Identifier: 0BSD

# pkg-config wrapper that outputs a makefile fragment

set -eu

NAME=$(printf '%s' "$1" | tr 'a-z' 'A-Z')
eval "XUSE=\"\${USE_$NAME:-}\""

if [ "$XUSE" ]; then
    USE="$XUSE"
elif config/pkgconf.sh "$1"; then
    USE=y
else
    USE=n
fi

if [ "$USE" = y ]; then
    printf 'PKGS += %s\n' "$1"
    printf 'CPPFLAGS += -DBFS_USE_%s=1\n' "$NAME"
else
    printf 'CPPFLAGS += -DBFS_USE_%s=0\n' "$NAME"
fi
