#!/usr/bin/env bash

# Copyright © Tavian Barnes <tavianator@tavianator.com>
# SPDX-License-Identifier: 0BSD

# pkg-config wrapper that outputs a makefile fragment

set -eu

NAME="${1^^}"
declare -n XUSE="XUSE_$NAME"

if [ "$XUSE" ]; then
    USE="$XUSE"
elif [[ "$NOLIBS" == *y* ]]; then
    USE=n
elif config/pkgconf.sh "$1"; then
    USE=y
else
    USE=n
fi

printf '%s := %s\n' "USE_$NAME" "$USE"

if [ "$USE" = y ]; then
    printf 'CPPFLAGS += -DBFS_USE_%s=1\n' "$NAME"

    CFLAGS=$(config/pkgconf.sh --cflags "$1")
    if [ "$CFLAGS" ]; then
        printf 'CFLAGS += %s\n' "$CFLAGS"
    fi

    LDFLAGS=$(config/pkgconf.sh --ldflags "$1")
    if [ "$LDFLAGS" ]; then
        printf 'LDFLAGS += %s\n' "$LDFLAGS"
    fi

    LDLIBS=$(config/pkgconf.sh --ldlibs "$1")
    if [ "$LDLIBS" ]; then
        printf 'LDLIBS += %s\n' "$LDLIBS"
    fi
else
    printf 'CPPFLAGS += -DBFS_USE_%s=0\n' "$NAME"
fi
