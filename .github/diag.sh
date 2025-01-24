#!/usr/bin/env bash

# Copyright © Tavian Barnes <tavianator@tavianator.com>
# SPDX-License-Identifier: 0BSD

# Convert compiler diagnostics to GitHub Actions messages
# https://docs.github.com/en/actions/using-workflows/workflow-commands-for-github-actions#setting-a-warning-message

set -eu

filter() {
    sed -En 'p; s/^([^:]*):([^:]*):([^:]*): (warning|error): (.*)$/::\4 file=\1,line=\2,col=\3,title=Compiler \4::\5/p'
}

exec "$@" > >(filter) 2> >(filter >&2)
