#!/usr/bin/env bash

# Copyright © Tavian Barnes <tavianator@tavianator.com>
# SPDX-License-Identifier: 0BSD

# Creates a directory tree that matches a git repo, but with empty files.  E.g.
#
#     $ ./bench/clone-tree.sh "https://.../linux.git" v6.5 ./linux ./linux.git
#
# will create or update a shallow clone at ./linux.git, then create a directory
# tree at ./linux with the same directory tree as the tag v6.5, except all files
# will be empty.

set -eu

if (($# != 4)); then
    printf 'Usage: %s https://url/of/repo.git <TAG> path/to/checkout path/to/repo.git\n' "$0" >&2
    exit 1
fi

URL="$1"
TAG="$2"
DIR="$3"
REPO="$4"

BENCH=$(dirname -- "${BASH_SOURCE[0]}")
BIN=$(realpath -- "$BENCH/../bin")
BFS="$BIN/bfs"
XTOUCH="$BIN/tests/xtouch"

if [ "${NPROC-}" ]; then
    # Use fewer cores in recursive calls
    export NPROC=$(((NPROC + 1) / 2))
else
    export NPROC=$(nproc)
fi

JOBS=$((NPROC < 8 ? NPROC : 8))

do-git() {
    git -C "$REPO" "$@"
}

if ! [ -e "$REPO" ]; then
    mkdir -p -- "$REPO"
    do-git init -q --bare
fi

has-ref() {
    do-git rev-list --quiet -1 --missing=allow-promisor "$1" &>/dev/null
}

sparse-fetch() {
    do-git -c fetch.negotiationAlgorithm=noop fetch -q --filter=blob:none --depth=1 --no-tags --no-write-fetch-head --no-auto-gc "$@"
}

if ! has-ref "$TAG"; then
    printf 'Fetching %s ...\n' "$TAG" >&2
    do-git config remote.origin.url "$URL"
    if ((${#TAG} >= 40)); then
        sparse-fetch origin "$TAG"
    else
        sparse-fetch origin tag "$TAG"
    fi
fi

# Delete a tree in parallel
clean() {
    local d=5
    "$BFS" -f "$1" -mindepth $d -maxdepth $d -type d -print0 \
        | xargs -0r -n1 -P$JOBS -- "$BFS" -j1 -mindepth 1 -delete -f
    "$BFS" -f "$1" -delete
}

if [ -e "$DIR" ]; then
    printf 'Cleaning old directory tree %s ...\n' "$DIR" >&2
    TMP=$(mktemp -dp "$(dirname -- "$DIR")")
    mv -- "$DIR" "$TMP"
    clean "$TMP" &
fi

# List gitlinks (submodule references) in the tree
ls-gitlinks() {
    do-git ls-tree -zr "$TAG" \
        | sed -zn 's/.* commit //p'
}

# Get the submodule ID for a path
submodule-for-path() {
    do-git config --blob "$TAG:.gitmodules" \
                  --name-only \
                  --fixed-value \
                  --get-regexp 'submodule\..**\.path' "$1" \
        | sed -En 's/submodule\.(.*)\.path/\1/p'
}

# Get the URL for a submodule
submodule-url() {
    # - https://chrome-internal.googlesource.com/
    #   - not publicly accessible
    # - https://chromium.googlesource.com/external/github.com/WebKit/webkit.git
    #   - is accessible, but the commit (59e9de61b7b3) isn't
    # - https://android.googlesource.com/
    #   - is accessible, but you need an account

    do-git config --blob "$TAG:.gitmodules" \
                  --get "submodule.$1.url" \
        | sed -E \
              -e '\|^https://chrome-internal.googlesource.com/|Q1' \
              -e '\|^https://chromium.googlesource.com/external/github.com/WebKit/webkit.git|Q1' \
              -e '\|^https://android.googlesource.com/|Q1'
}

# Recursively checkout submodules
while read -rd '' SUBREF SUBDIR; do
    SUBNAME=$(submodule-for-path "$SUBDIR")
    SUBURL=$(submodule-url "$SUBNAME") || continue

    if (($(jobs -pr | wc -w) >= JOBS)); then
        wait -n
    fi
    "$0" "$SUBURL" "$SUBREF" "$DIR/$SUBDIR" "$REPO/modules/$SUBNAME" &
done < <(ls-gitlinks)

# Touch files in parallel
xtouch() (
    cd "$DIR"
    if ((JOBS > 1)); then
        xargs -0r -n4096 -P$JOBS -- "$XTOUCH" -p --
    else
        xargs -0r -- "$XTOUCH" -p --
    fi
)

# Check out files
printf 'Checking out %s ...\n' "$DIR" >&2
mkdir -p -- "$DIR"
do-git ls-tree -zr "$TAG"\
    | sed -zn 's/.* blob .*\t//p' \
    | xtouch

# Wait for cleaning/submodules
wait
