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

    if ((COLOR_STDOUT || VERBOSE_TESTS)); then
        TEST_FMT="${BOL}${YLW}%s${RST}${EOL}"
    else
        TEST_FMT="."
    fi

    # Turn off set -e (but turn it back on in run_test)
    set +e

    for TEST in "${TEST_CASES[@]}"; do
        printf "$TEST_FMT" "$TEST"

        OUT="$TMP/$TEST.out"
        mkdir -p "${OUT%/*}"

        if ((VERBOSE_ERRORS)); then
            run_test "$TESTS/$TEST.sh"
        else
            run_test "$TESTS/$TEST.sh" 2>"$TMP/$TEST.err"
        fi
        status=$?

        if ((status == 0)); then
            ((++passed))
        elif ((status == EX_SKIP)); then
            ((++skipped))
        else
            ((++failed))
            ((VERBOSE_ERRORS)) || cat "$TMP/$TEST.err" >&2
            color printf "${BOL}${RED}%s failed!${RST}\n" "$TEST"
            ((STOP)) && break
        fi
    done

    printf "${BOL}"

    if ((passed > 0)); then
        color printf "${GRN}tests passed: %d${RST}\n" "$passed"
    fi
    if ((skipped > 0)); then
        color printf "${CYN}tests skipped: %s${RST}\n" "$skipped"
    fi
    if ((failed > 0)); then
        color printf "${RED}tests failed: %s${RST}\n" "$failed"
        exit 1
    fi
}

## Utilities for the tests themselves

# Return value when a test is skipped
EX_SKIP=77

# Skip the current test
skip() {
    if ((VERBOSE_SKIPPED)); then
        caller | {
            read -r line file
            printf "${BOL}"
            debug "$file" $line "${CYN}$TEST skipped!${RST}" "$(awk "NR == $line" "$file")" >&3
        }
    elif ((VERBOSE_TESTS)); then
        color printf "${BOL}${CYN}%s skipped!${RST}\n" "$TEST"
    fi

    exit $EX_SKIP
}

# Run a command and check its exit status
check_exit() {
    local expected="$1"
    local actual="0"
    shift
    "$@" || actual="$?"
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
    printf "${GRN}%q${RST} " "${BFS[@]}"

    local expr_started=
    for arg; do
        if [[ $arg == -[A-Z]* ]]; then
            printf "${CYN}%q${RST} " "$arg"
        elif [[ $arg == [\(!] || $arg == -[ao] || $arg == -and || $arg == -or || $arg == -not ]]; then
            expr_started=yes
            printf "${RED}%q${RST} " "$arg"
        elif [[ $expr_started && $arg == [\),] ]]; then
            printf "${RED}%q${RST} " "$arg"
        elif [[ $arg == -?* ]]; then
            expr_started=yes
            printf "${BLU}%q${RST} " "$arg"
        elif [ "$expr_started" ]; then
            printf "${BLD}%q${RST} " "$arg"
        else
            printf "${MAG}%q${RST} " "$arg"
        fi
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
        exit "$ret"
    else
        return "$ret"
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
        exit "$ret"
    else
        return "$ret"
    fi
}

# Create a directory tree with xattrs in scratch
make_xattrs() {
    clean_scratch

    "$XTOUCH" scratch/{normal,xattr,xattr_2}
    ln -s xattr scratch/link
    ln -s normal scratch/xattr_link

    case "$UNAME" in
        Darwin)
            xattr -w bfs_test true scratch/xattr \
                && xattr -w bfs_test_2 true scratch/xattr_2 \
                && xattr -s -w bfs_test true scratch/xattr_link
            ;;
        FreeBSD)
            setextattr user bfs_test true scratch/xattr \
                && setextattr user bfs_test_2 true scratch/xattr_2 \
                && setextattr -h user bfs_test true scratch/xattr_link
            ;;
        *)
            # Linux tmpfs doesn't support the user.* namespace, so we use the security.*
            # namespace, which is writable by root and readable by others
            bfs_sudo setfattr -n security.bfs_test scratch/xattr \
                && bfs_sudo setfattr -n security.bfs_test_2 scratch/xattr_2 \
                && bfs_sudo setfattr -h -n security.bfs_test scratch/xattr_link
            ;;
    esac
}

## Snapshot testing

# Return value when a difference is detected
EX_DIFF=20

# Detect colored diff support
if ((COLOR_STDERR)) && diff --color=always /dev/null /dev/null 2>/dev/null; then
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
    else
        $DIFF -u "$GOLD" "$OUT" >&2
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
