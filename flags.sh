#!/usr/bin/env bash

set -eu

OUT="$1"
shift

echo "$@" >"$OUT.tmp"

if [ -e "$OUT" ] && cmp -s "$OUT" "$OUT.tmp"; then
    rm "$OUT.tmp"
else
    mv "$OUT.tmp" "$OUT"
fi
