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
ROOT=$(_realpath "$(dirname -- "$TESTS")")
TESTS="$ROOT/tests"
BIN="$ROOT/bin"
MKSOCK="$BIN/tests/mksock"
XTOUCH="$BIN/tests/xtouch"
UNAME=$(uname)

# Standardize the environment
stdenv() {
    export LC_ALL=C
    export TZ=UTC0

    local SAN_OPTIONS="abort_on_error=1:halt_on_error=1:log_to_syslog=0"
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

    # Count the inherited FDs
    if [ -d /proc/self/fd ]; then
        local fds=/proc/self/fd
    else
        local fds=/dev/fd
    fi
    # We use ls $fds on purpose, rather than e.g. ($fds/*), to avoid counting
    # internal bash fds that are not exposed to spawned processes
    NOPENFD=$(ls -1q "$fds/" 2>/dev/null | wc -l)
    NOPENFD=$((NOPENFD > 3 ? NOPENFD - 1 : 3))

    # Close stdin so bfs doesn't think we're interactive
    # dup() the standard fds for logging even when redirected
    exec </dev/null {DUPOUT}>&1 {DUPERR}>&2
}

# Drop root priviliges or bail
drop_root() {
    if command -v capsh &>/dev/null; then
        if capsh --has-p=cap_dac_override &>/dev/null || capsh --has-p=cap_dac_read_search &>/dev/null; then
	    if [ -n "${BFS_TRIED_DROP:-}" ]; then
                color cat >&2 <<EOF
${RED}error:${RST} Failed to drop capabilities.
EOF

	        exit 1
	    fi

            color cat >&2 <<EOF
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

        color cat >&2 <<EOF
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
    local file="$1"
    local line="$2"
    local msg="$3"
    local cmd="$(awk "NR == $line" "$file" 2>/dev/null)" || :
    file="${file/#*\/tests\//tests/}"

    color printf "${BLD}%s:%d:${RST} %s\n    %s\n" "$file" "$line" "$msg" "$cmd"
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

DEFER_LEVEL=-1

# Run a command when this (sub)shell exits
defer() {
    # Check if the EXIT trap is already set
    if ((DEFER_LEVEL != BASH_SUBSHELL)); then
        DEFER_LEVEL=$BASH_SUBSHELL
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
    local cmd="${DEFER_CMDS[-1]}"
    local file="${DEFER_FILES[-1]}"
    local line="${DEFER_LINES[-1]}"
    unset "DEFER_CMDS[-1]"
    unset "DEFER_FILES[-1]"
    unset "DEFER_LINES[-1]"

    local ret=0
    eval "$cmd" || ret=$?

    if ((ret != 0)); then
        debug "$file" $line "${RED}error $ret${RST}" >&$DUPERR
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
