#!/usr/bin/env bash

# Copyright © Tavian Barnes <tavianator@tavianator.com>
# SPDX-License-Identifier: 0BSD

# Prints the "ground truth" coloring of a path using ls

set -e

function parse_ls_colors() {
    for key; do
        local -n var="$key"
        if [[ "$LS_COLORS" =~ (^|:)$key=(([^:]|\\:)*) ]]; then
            var="${BASH_REMATCH[2]}"
            # Interpret escapes
            var=$(printf "$var" | sed $'s/\^\[/\033/g; s/\\\\:/:/g')
        fi
    done
}

function re_escape() {
    # https://stackoverflow.com/a/29613573/502399
    sed 's/[^^]/[&]/g; s/\^/\\^/g' <<<"$1"
}

rs=0
lc=$'\033['
rc=m
ec=
no=

parse_ls_colors rs lc rc ec no
: "${ec:=$lc$rs$rc}"

strip="(($(re_escape "$lc$no$rc"))?($(re_escape "$ec")|$(re_escape "$lc$rc")))+"

function ls_color() {
    # Strip the leading reset sequence from the ls output
    ls -1d --color "$@" | sed -E "s/^$strip([a-z].*)$strip/\4/; s/^$strip//"
}

L=
if [ "$1" = "-L" ]; then
    L="$1"
    shift
fi

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
