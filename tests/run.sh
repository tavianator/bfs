#!/hint/bash

# Copyright © Tavian Barnes <tavianator@tavianator.com>
# SPDX-License-Identifier: 0BSD

## Running test cases

# ERR trap for tests
debug_err() {
    local ret=$? line func file
    callers | while read -r line func file; do
        if [ "$func" = source ]; then
            debug "$file" $line "${RED}error $ret${RST}" >&$DUPERR
            break
        fi
    done
}

# Source a test
source_test() (
    set -eE
    trap debug_err ERR

    if ((${#MAKE[@]})); then
        # Close the jobserver pipes
        exec {READY_PIPE}<&- {DONE_PIPE}>&-
    fi

    cd "$TMP"
    source "$@"
)

# Run a test
run_test() {
    if ((VERBOSE_ERRORS)); then
        source_test "$1"
    else
        source_test "$1" 2>"$TMP/$TEST.err"
    fi
    ret=$?

    if ((${#MAKE[@]})); then
        # Write one byte to the done pipe
        printf . >&$DONE_PIPE
    fi

    case $ret in
        0)
            if ((VERBOSE_TESTS)); then
                color printf "${GRN}[PASS]${RST} ${BLD}%s${RST}\n" "$TEST"
            fi
            ;;
        $EX_SKIP)
            if ((VERBOSE_SKIPPED || VERBOSE_TESTS)); then
                color printf "${CYN}[SKIP]${RST} ${BLD}%s${RST}\n" "$TEST"
            fi
            ;;
        *)
            if ((!VERBOSE_ERRORS)); then
                cat "$TMP/$TEST.err" >&2
            fi
            color printf "${RED}[FAIL]${RST} ${BLD}%s${RST}\n" "$TEST"
            ;;
    esac

    return $ret
}

# Count the tests running in the background
BG=0

# Run a test in the background
bg_test() {
    run_test "$1" &
    ((++BG))
}

# Reap a finished background test
reap_test() {
    ((BG--))

    case "$1" in
        0)
            ((++passed))
            ;;
        $EX_SKIP)
            ((++skipped))
            ;;
        *)
            ((++failed))
            ;;
    esac
}

# Wait for a background test to finish
wait_test() {
    local pid line

    while true; do
        line=$((LINENO + 1))
        wait -n -ppid
        ret=$?

        if [ "${pid:-}" ]; then
            break
        elif ((ret > 128)); then
            # Interrupted by signal
            continue
        else
            debug "${BASH_SOURCE[0]}" $line "${RED}error $ret${RST}" >&$DUPERR
            exit 1
        fi
    done

    reap_test $ret
}

# Wait until we're ready to run another test
wait_ready() {
    if ((${#MAKE[@]})); then
        # We'd like to parse the output of jobs -n, but we can't run it in a
        # subshell or we won't get the right output
        jobs -n >"$TMP/jobs"
        while read -r job status ret foo; do
            case "$status" in
                Done)
                    reap_test 0
                    ;;
                Exit)
                    reap_test $ret
                    ;;
            esac
        done <"$TMP/jobs"

        # Read one byte from the ready pipe
        read -r -N1 -u$READY_PIPE
    elif ((BG >= JOBS)); then
        wait_test
    fi
}

# Run make as a co-process to use its job control
comake() {
    coproc {
        # We can't just use std{in,out}, due to
        # https://www.gnu.org/software/make/manual/html_node/Parallel-Input.html
        exec {DONE_PIPE}<&0 {READY_PIPE}>&1
        exec "${MAKE[@]}" -s \
             -f "$TESTS/tests.mk" \
             DONE=$DONE_PIPE \
             READY=$READY_PIPE \
             "${!TEST_CASES[@]}" \
             </dev/null >/dev/null
    }

    # coproc pipes aren't inherited by subshells, so dup them
    exec {READY_PIPE}<&${COPROC[0]} {DONE_PIPE}>&${COPROC[1]}
}

# Print the current test progess
progress() {
    if [ "${BAR:-}" ]; then
        print_bar "$(printf "$@")"
    elif ((VERBOSE_TESTS)); then
         color printf "$@"
    fi
}

# Run all the tests
run_tests() {
    passed=0
    failed=0
    skipped=0
    ran=0
    total=${#TEST_CASES[@]}

    TEST_FMT="${YLW}[%3d%%]${RST} ${BLD}%s${RST}\\n"

    if ((${#MAKE[@]})); then
        comake
    fi

    # Turn off set -e (but turn it back on in run_test)
    set +e

    if ((COLOR_STDOUT && !VERBOSE_TESTS)); then
        show_bar
    fi

    for TEST in "${TEST_CASES[@]}"; do
        wait_ready
        if ((STOP && failed > 0)); then
            break
        fi

        percent=$((100 * ran / total))
        progress "${YLW}[%3d%%]${RST} ${BLD}%s${RST}\\n" $percent "$TEST"

        mkdir -p "$TMP/$TEST"
        OUT="$TMP/$TEST.out"

        bg_test "$TESTS/$TEST.sh"
        ((++ran))
    done

    while ((BG > 0)); do
        wait_test
    done

    if [ "${BAR:-}" ]; then
        hide_bar
    fi

    if ((passed > 0)); then
        color printf "${GRN}[PASS]${RST} ${BLD}%3d${RST} / ${BLD}%d${RST}\n" $passed $total
    fi
    if ((skipped > 0)); then
        color printf "${CYN}[SKIP]${RST} ${BLD}%3d${RST} / ${BLD}%d${RST}\n" $skipped $total
    fi
    if ((failed > 0)); then
        color printf "${RED}[FAIL]${RST} ${BLD}%3d${RST} / ${BLD}%d${RST}\n" $failed $total
        exit 1
    fi
}

## Utilities for the tests themselves

# Default return value for failed tests
EX_FAIL=1

# Fail the current test
fail() {
    exit $EX_FAIL
}

# Return value when a test is skipped
EX_SKIP=77

# Skip the current test
skip() {
    if ((VERBOSE_SKIPPED)); then
        caller | {
            read -r line file
            debug "$file" $line "" >&$DUPOUT
        }
    fi

    exit $EX_SKIP
}

# Run a command and check its exit status
check_exit() {
    local expected="$1"
    local actual=0
    shift
    "$@" || actual=$?
    ((actual == expected))
}

# Run a command with sudo
bfs_sudo() {
    if ((${#SUDO[@]})); then
        "${SUDO[@]}" "$@"
    else
        return 1
    fi
}

# Get the inode number of a file
inum() {
    ls -id "$@" | awk '{ print $1 }'
}

# Set an ACL on a file
set_acl() {
    case "$UNAME" in
        Darwin)
            chmod +a "$(id -un) allow read,write" "$1"
            ;;
        FreeBSD)
            if (($(getconf ACL_NFS4 "$1") > 0)); then
                setfacl -m "u:$(id -un):rw::allow" "$1"
            else
                setfacl -m "u:$(id -un):rw" "$1"
            fi
            ;;
        *)
            setfacl -m "u:$(id -un):rw" "$1"
            ;;
    esac
}

# Print a bfs invocation for --verbose=commands
bfs_verbose() {
    if ((VERBOSE_COMMANDS)); then
        (
            # Close some fds to make room for the pipe,
            # even with extremely low ulimit -n
            exec >&- {DUPERR}>&-
            exec >&$DUPOUT {DUPOUT}>&-
            color bfs_verbose_impl "$@"
        )
    fi
}

bfs_verbose_impl() {
    printf "${GRN}%q${RST}" "${BFS[0]}"
    if ((${#BFS[@]} > 1)); then
        printf " ${GRN}%q${RST}" "${BFS[@]:1}"
    fi

    local expr_started=0 color
    for arg; do
        case "$arg" in
            -[A-Z]*|-[dsxf]|-j*)
                color="${CYN}"
                ;;
            \(|!|-[ao]|-and|-or|-not|-exclude)
                expr_started=1
                color="${RED}"
                ;;
            \)|,)
                if ((expr_started)); then
                    color="${RED}"
                else
                    color="${MAG}"
                fi
                ;;
            -?*)
                expr_started=1
                color="${BLU}"
                ;;
            *)
                if ((expr_started)); then
                    color="${BLD}"
                else
                    color="${MAG}"
                fi
                ;;
        esac
        printf " ${color}%q${RST}" "$arg"
    done

    printf '\n'
}

# Run the bfs we're testing
invoke_bfs() {
    bfs_verbose "$@"

    local ret=0
    # Close the logging fds
    "${BFS[@]}" "$@" {DUPOUT}>&- {DUPERR}>&- || ret=$?

    # Allow bfs to fail, but not crash
    if ((ret > 125)); then
        exit $ret
    else
        return $ret
    fi
}

if command -v unbuffer &>/dev/null; then
    UNBUFFER=unbuffer
elif command -v expect_unbuffer &>/dev/null; then
    UNBUFFER=expect_unbuffer
fi

# Run bfs with a pseudo-terminal attached
bfs_pty() {
    test -n "${UNBUFFER:-}" || skip

    bfs_verbose "$@"

    local ret=0
    "$UNBUFFER" bash -c 'stty cols 80 rows 24 && "$@" </dev/null' bash "${BFS[@]}" "$@" || ret=$?

    if ((ret > 125)); then
        exit $ret
    else
        return $ret
    fi
}

# Create a directory tree with xattrs in scratch
make_xattrs() {
    cd "$TEST"

    "$XTOUCH" normal xattr xattr_2
    ln -s xattr link
    ln -s normal xattr_link

    case "$UNAME" in
        Darwin)
            xattr -w bfs_test true xattr \
                && xattr -w bfs_test_2 true xattr_2 \
                && xattr -s -w bfs_test true xattr_link
            ;;
        FreeBSD)
            setextattr user bfs_test true xattr \
                && setextattr user bfs_test_2 true xattr_2 \
                && setextattr -h user bfs_test true xattr_link
            ;;
        *)
            # Linux tmpfs doesn't support the user.* namespace, so we use the security.*
            # namespace, which is writable by root and readable by others
            bfs_sudo setfattr -n security.bfs_test xattr \
                && bfs_sudo setfattr -n security.bfs_test_2 xattr_2 \
                && bfs_sudo setfattr -h -n security.bfs_test xattr_link
            ;;
    esac
}

# Get the Unix epoch time in seconds
epoch_time() {
    # https://stackoverflow.com/a/12746260/502399
    awk 'BEGIN { srand(); print srand(); }'
}

## Snapshot testing

# Return value when a difference is detected
EX_DIFF=20

# Detect colored diff support
if ((COLOR_STDERR)) && diff --color=always /dev/null /dev/null &>/dev/null; then
    DIFF="diff --color=always"
else
    DIFF="diff"
fi

# Sort the output file
sort_output() {
    sort -o "$OUT" "$OUT"
}

# Diff against the expected output
diff_output() {
    local GOLD="$TESTS/$TEST.out"

    if ((UPDATE)); then
        cp "$OUT" "$GOLD"
    elif ! cmp -s "$GOLD" "$OUT"; then
        $DIFF -u "$GOLD" "$OUT" >&$DUPERR
    fi
}

# Run bfs, and diff it against the expected output
bfs_diff() {
    local ret=0
    invoke_bfs "$@" >"$OUT" || ret=$?

    sort_output
    diff_output || exit $EX_DIFF

    return $ret
}
