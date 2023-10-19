#!/hint/bash

# Copyright © Tavian Barnes <tavianator@tavianator.com>
# SPDX-License-Identifier: 0BSD

## Utility functions

# Portable realpath(1)
_realpath() (
    cd "$(dirname -- "$1")"
    echo "$PWD/$(basename -- "$1")"
)

# Globals
TESTS=$(_realpath "$TESTS")
if [ "${BUILDDIR-}" ]; then
    BIN=$(_realpath "$BUILDDIR/bin")
else
    BIN=$(_realpath "$TESTS/../bin")
fi
MKSOCK="$BIN/tests/mksock"
XTOUCH="$BIN/tests/xtouch"
UNAME=$(uname)

# Standardize the environment
stdenv() {
    export LC_ALL=C
    export TZ=UTC0

    local SAN_OPTIONS="halt_on_error=1:log_to_syslog=0"
    export ASAN_OPTIONS="$SAN_OPTIONS"
    export LSAN_OPTIONS="$SAN_OPTIONS"
    export MSAN_OPTIONS="$SAN_OPTIONS"
    export TSAN_OPTIONS="$SAN_OPTIONS"
    export UBSAN_OPTIONS="$SAN_OPTIONS"

    export LS_COLORS=""
    unset BFS_COLORS

    if [ "$UNAME" = Darwin ]; then
        # ASan on macOS likes to report
        #
        #     malloc: nano zone abandoned due to inability to preallocate reserved vm space.
        #
        # to syslog, which as a side effect opens a socket which might take the
        # place of one of the standard streams if the process is launched with
        # it closed.  This environment variable avoids the message.
        export MallocNanoZone=0
    fi

    # Close non-standard inherited fds
    if [ -d /proc/self/fd ]; then
        local fds=/proc/self/fd
    else
        local fds=/dev/fd
    fi

    for fd in "$fds"/*; do
        if [ ! -e "$fd" ]; then
            continue
        fi

        local fd="${fd##*/}"
        if ((fd > 2)); then
            eval "exec ${fd}<&-"
        fi
    done

    # Close stdin so bfs doesn't think we're interactive
    # dup() the standard fds for logging even when redirected
    exec </dev/null 3>&1 4>&2
}

# Drop root priviliges or bail
drop_root() {
    if command -v capsh &>/dev/null; then
        if capsh --has-p=cap_dac_override &>/dev/null || capsh --has-p=cap_dac_read_search &>/dev/null; then
	    if [ -n "${BFS_TRIED_DROP:-}" ]; then
                color >&2 <<EOF
${RED}error:${RST} Failed to drop capabilities.
EOF

	        exit 1
	    fi

            color >&2 <<EOF
${YLW}warning:${RST} Running as ${BLD}$(id -un)${RST} is not recommended.  Dropping ${BLD}cap_dac_override${RST} and
${BLD}cap_dac_read_search${RST}.

EOF

            BFS_TRIED_DROP=y exec capsh \
                --drop=cap_dac_override,cap_dac_read_search \
                --caps=cap_dac_override,cap_dac_read_search-eip \
                -- "$0" "$@"
        fi
    elif ((EUID == 0)); then
        UNLESS=
        if [ "$UNAME" = "Linux" ]; then
	    UNLESS=" unless ${GRN}capsh${RST} is installed"
        fi

        color >&2 <<EOF
${RED}error:${RST} These tests expect filesystem permissions to be enforced, and therefore
will not work when run as ${BLD}$(id -un)${RST}${UNLESS}.
EOF
        exit 1
    fi
}

## Debugging

# Get the bash call stack
callers() {
    local frame=0
    while caller $frame; do
        ((++frame))
    done
}

# Print a message including path, line number, and command
debug() {
    local file="${1/#*\/tests\//tests\/}"
    set -- "$file" "${@:2}"
    cprintf "${BLD}%s:%d:${RST} %s\n    %s\n" "$@"
}

## Deferred cleanup

# Quote a command safely for eval
quote() {
    printf '%q' "$1"
    shift
    if (($# > 0)); then
        printf ' %q' "$@"
    fi
}

# Run a command when this (sub)shell exits
defer() {
    # Refresh trap state before trap -p
    # See https://unix.stackexchange.com/a/556888/56202
    trap -- KILL

    # Check if the EXIT trap is already set
    if ! trap -p EXIT | grep -q pop_defers; then
        DEFER_CMDS=()
        DEFER_LINES=()
        DEFER_FILES=()
        trap pop_defers EXIT
    fi

    DEFER_CMDS+=("$(quote "$@")")

    local line file
    read -r line file < <(caller)
    DEFER_LINES+=("$line")
    DEFER_FILES+=("$file")
}

# Pop a single command from the defer stack and run it
pop_defer() {
    local i=$((${#DEFER_CMDS[@]} - 1))
    local cmd="${DEFER_CMDS[$i]}"
    local file="${DEFER_FILES[$i]}"
    local line="${DEFER_LINES[$i]}"
    unset "DEFER_CMDS[$i]"
    unset "DEFER_FILES[$i]"
    unset "DEFER_LINES[$i]"

    local ret=0
    eval "$cmd" || ret=$?

    if ((ret != 0)); then
        debug "$file" $line "${RED}error $ret${RST}" "defer $cmd" >&4
    fi

    return $ret
}

# Run all deferred commands
pop_defers() {
    local ret=0

    while ((${#DEFER_CMDS[@]} > 0)); do
        pop_defer || ret=$?
    done

    return $ret
}
