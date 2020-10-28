#!/bin/bash

set -e

echo "$@" >.newflags

if [ -e .flags ] && cmp -s .flags .newflags; then
    rm .newflags
else
    mv .newflags .flags
fi
