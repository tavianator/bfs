#!/hint/bash

# Copyright © Tavian Barnes <tavianator@tavianator.com>
# SPDX-License-Identifier: 0BSD

## Running test cases

# Beginning/end of line escape sequences
BOL=$'\n'
EOL=$'\n'

# Update $EOL for the terminal size
update_eol() {
    # Bash gets $COLUMNS from stderr, so if it's redirected use tput instead
    local cols="${COLUMNS-}"
    if [ -z "$cols" ]; then
        cols=$(tput cols 2>/dev/tty)
    fi

    # Put the cursor at the last column, then write a space so the next
    # character will wrap
    EOL=$'\e['"${cols}G "
}

# ERR trap for tests
debug_err() {
    local ret=$? line func file
    callers | while read -r line func file; do
        if [ "$func" = source ]; then
            local cmd="$(awk "NR == $line" "$file" 2>/dev/null)" || :
            debug "$file" $line "${RED}error $ret${RST}" "$cmd" >&4
            break
        fi
    done
}

# Run a single test
run_test() (
    set -eE
    trap debug_err ERR
    cd "$TMP"
    source "$@"
)

# Run a test in the background
bg_test() {
    if ((VERBOSE_ERRORS)); then
        run_test "$1"
    else
        run_test "$1" 2>"$TMP/$TEST.err"
    fi
    ret=$?

    case $ret in
        0)
            if ((VERBOSE_TESTS)); then
                color printf "${BOL}${GRN}[PASS]${RST} ${BLD}%s${RST}\n" "$TEST"
            fi
            ;;
        $EX_SKIP)
            if ((VERBOSE_SKIPPED || VERBOSE_TESTS)); then
                color printf "${BOL}${CYN}[SKIP]${RST} ${BLD}%s${RST}\n" "$TEST"
            fi
            ;;
        *)
            if ((!VERBOSE_ERRORS)); then
                cat "$TMP/$TEST.err" >&2
            fi
            color printf "${BOL}${RED}[FAIL]${RST} ${BLD}%s${RST}\n" "$TEST"
            ;;
    esac

    return $ret
}

# Wait for any background job to complete
if ((BASH_VERSINFO[0] > 4 || (BASH_VERSINFO[0] == 4 && BASH_VERSINFO[1] >= 3))); then
    wait_any() {
        wait -n
    }
else
    wait_any() {
        read -ra jobs < <(jobs -p)
        wait ${jobs[0]}
    }
fi

# Wait for a background test to finish
wait_test() {
    wait_any
    ret=$?
    ((BG--))

    case $ret in
        0)
            ((++passed))
            return 0
            ;;
        $EX_SKIP)
            ((++skipped))
            return 0
            ;;
        *)
            ((++failed))
            return $ret
            ;;
    esac
}

# Run all the tests
run_tests() {
    if ((VERBOSE_TESTS)); then
        BOL=''
    elif ((COLOR_STDOUT)); then
        # Carriage return + clear line
        BOL=$'\r\e[K'

        # Workaround for bash 4: checkwinsize is off by default.  We can turn it
        # on, but we also have to explicitly trigger a foreground job to finish
        # so that it will update the window size before we use $COLUMNS
        shopt -s checkwinsize
        (:)

        update_eol
        trap update_eol WINCH
    fi

    passed=0
    failed=0
    skipped=0
    ran=0
    total=${#TEST_CASES[@]}

    if ((COLOR_STDOUT || VERBOSE_TESTS)); then
        TEST_FMT="${BOL}${YLW}[%3d%%]${RST} ${BLD}%s${RST}${EOL}"
    else
        TEST_FMT="."
    fi

    BG=0

    # Turn off set -e (but turn it back on in run_test)
    set +e

    for TEST in "${TEST_CASES[@]}"; do
        if ((BG >= JOBS)); then
            wait_test
            if (($? && STOP)); then
                break
            fi
        fi

        percent=$((100 * ran / total))
        color printf "$TEST_FMT" $percent "$TEST"

        mkdir -p "$TMP/$TEST"
        OUT="$TMP/$TEST.out"

        bg_test "$TESTS/$TEST.sh" &
        ((++BG, ++ran))
    done

    while ((BG > 0)); do
        wait_test
    done

    printf "${BOL}"

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
            printf "${BOL}"
            debug "$file" $line "" "$(awk "NR == $line" "$file")" >&3
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
            exec >&- 4>&-
            exec >&3 3>&-
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
    "${BFS[@]}" "$@" 3>&- 4>&- || ret=$?

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
    "$UNBUFFER" bash -c 'stty cols 80 rows 24 && "$@"' bash "${BFS[@]}" "$@" || ret=$?

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

## Snapshot testing

# Return value when a difference is detected
EX_DIFF=20

# Detect colored diff support
if diff --color /dev/null /dev/null 2>/dev/null; then
    DIFF="diff --color"
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
    else
        $DIFF -u "$GOLD" "$OUT" >&4
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
