#!/usr/bin/env bash

# Copyright © Tavian Barnes <tavianator@tavianator.com>
# SPDX-License-Identifier: 0BSD

# Prints the "ground truth" coloring of a path using ls

set -e

L=
if [ "$1" = "-L" ]; then
    L="$1"
    shift
fi

function ls_color() {
    # Strip the leading reset sequence from the ls output
    ls -1d --color "$@" | sed $'s/^\033\\[0m//'
}

DIR="${1%/*}"
if [ "$DIR" = "$1" ]; then
    ls_color "$1"
    exit
fi

BASE="${1##*/}"

ls_color $L "$DIR/" | tr -d '\n'

if [ -e "$1" ]; then
    (cd "$DIR" && ls_color $L "$BASE")
else
    (cd "$DIR" && ls_color "$BASE")
fi
