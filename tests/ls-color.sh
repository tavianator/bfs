#!/bin/bash

############################################################################
# bfs                                                                      #
# Copyright (C) 2019 Tavian Barnes <tavianator@tavianator.com>             #
#                                                                          #
# Permission to use, copy, modify, and/or distribute this software for any #
# purpose with or without fee is hereby granted.                           #
#                                                                          #
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES #
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF         #
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR  #
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES   #
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN    #
# ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF  #
# OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.           #
############################################################################

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
