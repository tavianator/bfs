#!/usr/bin/env bash

# Copyright © Tavian Barnes <tavianator@tavianator.com>
# SPDX-License-Identifier: 0BSD

# Convert compiler diagnostics to GitHub Actions messages
# https://docs.github.com/en/actions/using-workflows/workflow-commands-for-github-actions#setting-a-warning-message

set -eu

filter() {
    sed -E 's/^(([^:]*):([^:]*):([^:]*): (warning|error): (.*))$/::\5 file=\2,line=\3,col=\4,title=Compiler \5::\6\
\1/'
}

exec "$@" > >(filter) 2> >(filter >&2)
