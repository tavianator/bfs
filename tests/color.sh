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

# Save this in case the tests unset PATH
SED=$(command -v sed)

# Filter out escape sequences if necessary
color() {
    if color_fd 1; then
        "$@"
    else
        "$@" | "$SED" $'s/\e\\[[^m]*m//g'
    fi
}

## Status bar

# Show the terminal status bar
show_bar() {
    if [ -z "$TTY" ]; then
        return 1
    fi

    # Name the pipe deterministically based on the ttyname, so that concurrent
    # tests.sh runs on the same terminal (e.g. make -jN check) cooperate
    local pipe
    pipe=$(printf '%s' "$TTY" | tr '/' '-')
    pipe="${TMPDIR:-/tmp}/bfs$pipe.bar"

    if mkfifo "$pipe" 2>/dev/null; then
        # We won the race, create the background process to manage the bar
        bar_proc "$pipe" &
        exec {BAR}>"$pipe"
    elif [ -p "$pipe" ]; then
        # We lost the race, connect to the existing process.
        # There is a small TOCTTOU race here but I don't see how to avoid it.
        exec {BAR}>"$pipe"
    else
        return 1
    fi
}

# Print to the terminal status bar
print_bar() {
    printf 'PRINT:%d:%s\0' $$ "$1" >&$BAR
}

# Hide the terminal status bar
hide_bar() {
    exec {BAR}>&-
    unset BAR
}

# The background process that muxes multiple status bars for one TTY
bar_proc() {
    # Read from the pipe, write to the TTY
    exec <"$1" >"$TTY"

    # Delete the pipe when done
    defer rm "$1"
    # Reset the scroll region when done
    defer printf '\e7\e[r\e8\e[J'

    # Workaround for bash 4: checkwinsize is off by default.  We can turn it
    # on, but we also have to explicitly trigger a foreground job to finish
    # so that it will update the window size before we use $LINES
    shopt -s checkwinsize
    (:)

    BAR_HEIGHT=0
    resize_bar
    # Adjust the bar when the TTY size changes
    trap resize_bar WINCH

    # Map from PID to status bar
    local -A pid2bar

    # Read commands of the form "OP:PID:STRING\0"
    while IFS=':' read -r -d '' op pid str; do
        # Map the pid to a bar, creating a new one if necessary
        if [ -z "${pid2bar[$pid]:-}" ]; then
            pid2bar["$pid"]=$((BAR_HEIGHT++))
            resize_bar
        fi
        bar="${pid2bar[$pid]}"

        case "$op" in
            PRINT)
                printf '\e7\e[%d;0f\e[K%s\e8' $((TTY_HEIGHT - bar)) "$str"
                ;;
        esac
    done
}

# Resize the status bar
resize_bar() {
    # Bash gets $LINES from stderr, so if it's redirected use tput instead
    TTY_HEIGHT="${LINES:-$(tput lines 2>"$TTY")}"

    if ((BAR_HEIGHT == 0)); then
        return
    fi

    # Hide the bars temporarily
    local seq='\e7\e[r\e8\e[J'
    # Print \eD (IND) N times to ensure N blank lines at the bottom
    for ((i = 0; i < BAR_HEIGHT; ++i)); do
        seq="${seq}\\eD"
    done
    # Go back up N lines
    seq="${seq}\\e[${BAR_HEIGHT}A"
    # Create the new scroll region
    seq="${seq}\\e7\\e[;$((TTY_HEIGHT - BAR_HEIGHT))r\\e8\\e[J"
    printf "$seq"
}
