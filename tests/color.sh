#!/hint/bash

# Copyright © Tavian Barnes <tavianator@tavianator.com>
# SPDX-License-Identifier: 0BSD

## Colored output

# Common escape sequences
BLD=$'\e[01m'
RED=$'\e[01;31m'
GRN=$'\e[01;32m'
YLW=$'\e[01;33m'
BLU=$'\e[01;34m'
MAG=$'\e[01;35m'
CYN=$'\e[01;36m'
RST=$'\e[0m'

# Check if we should color output to the given fd
color_fd() {
    [ -z "${NO_COLOR:-}" ] && [ -t "$1" ]
}

# Cache the color status for std{out,err}
color_fd 1 && COLOR_STDOUT=1 || COLOR_STDOUT=0
color_fd 2 && COLOR_STDERR=1 || COLOR_STDERR=0

# Save these in case the tests unset PATH
CAT=$(command -v cat)
SED=$(command -v sed)

# Filter out escape sequences if necessary
color() {
    if color_fd 1; then
        "$CAT"
    else
        "$SED" $'s/\e\\[[^m]*m//g'
    fi
}

# printf with auto-detected color support
cprintf() {
    printf "$@" | color
}
