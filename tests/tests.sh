#!/usr/bin/env bash

# Copyright © Tavian Barnes <tavianator@tavianator.com>
# SPDX-License-Identifier: 0BSD

set -euP
umask 022

export LC_ALL=C
export TZ=UTC0

SAN_OPTIONS="halt_on_error=1:log_to_syslog=0"
export ASAN_OPTIONS="$SAN_OPTIONS"
export LSAN_OPTIONS="$SAN_OPTIONS"
export MSAN_OPTIONS="$SAN_OPTIONS"
export TSAN_OPTIONS="$SAN_OPTIONS"
export UBSAN_OPTIONS="$SAN_OPTIONS"

export LS_COLORS=""
unset BFS_COLORS

BLD=$'\e[01m'
RED=$'\e[01;31m'
GRN=$'\e[01;32m'
YLW=$'\e[01;33m'
BLU=$'\e[01;34m'
MAG=$'\e[01;35m'
CYN=$'\e[01;36m'
RST=$'\e[0m'

function color_fd() {
    [ -z "${NO_COLOR:-}" ] && [ -t "$1" ]
}

color_fd 1 && COLOR_STDOUT=1 || COLOR_STDOUT=0
color_fd 2 && COLOR_STDERR=1 || COLOR_STDERR=0

# Filter out escape sequences if necessary
function color() {
    if color_fd 1; then
        cat
    else
        sed $'s/\e\\[[^m]*m//g'
    fi
}

# printf with auto-detected color support
function cprintf() {
    printf "$@" | color
}

UNAME=$(uname)

if [ "$UNAME" = Darwin ]; then
    # ASan on macOS likes to report
    #
    #     malloc: nano zone abandoned due to inability to preallocate reserved vm space.
    #
    # to syslog, which as a side effect opens a socket which might take the
    # place of one of the standard streams if the process is launched with it
    # closed.  This environment variable avoids the message.
    export MallocNanoZone=0
fi

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

function usage() {
    local pad=$(printf "%*s" ${#0} "")
    color <<EOF
Usage: ${GRN}$0${RST} [${BLU}--bfs${RST}=${MAG}path/to/bfs${RST}] [${BLU}--sudo${RST}[=${BLD}COMMAND${RST}]] [${BLU}--stop${RST}]
       $pad [${BLU}--no-clean${RST}] [${BLU}--update${RST}] [${BLU}--verbose${RST}[=${BLD}LEVEL${RST}]] [${BLU}--help${RST}]
       $pad [${BLU}--posix${RST}] [${BLU}--bsd${RST}] [${BLU}--gnu${RST}] [${BLU}--all${RST}] [${BLD}TEST${RST} [${BLD}TEST${RST} ...]]

  ${BLU}--bfs${RST}=${MAG}path/to/bfs${RST}
      Set the path to the bfs executable to test (default: ${MAG}./bin/bfs${RST})

  ${BLU}--sudo${RST}[=${BLD}COMMAND${RST}]
      Run tests that require root using ${GRN}sudo${RST} or the given ${BLD}COMMAND${RST}

  ${BLU}--stop${RST}
      Stop when the first error occurs

  ${BLU}--no-clean${RST}
      Keep the test directories around after the run

  ${BLU}--update${RST}
      Update the expected outputs for the test cases

  ${BLU}--verbose${RST}=${BLD}commands${RST}
      Log the commands that get executed
  ${BLU}--verbose${RST}=${BLD}errors${RST}
      Don't redirect standard error
  ${BLU}--verbose${RST}=${BLD}skipped${RST}
      Log which tests get skipped
  ${BLU}--verbose${RST}=${BLD}tests${RST}
      Log all tests that get run
  ${BLU}--verbose${RST}
      Log everything

  ${BLU}--help${RST}
      This message

  ${BLU}--posix${RST}, ${BLU}--bsd${RST}, ${BLU}--gnu${RST}, ${BLU}--all${RST}
      Choose which test cases to run (default: ${BLU}--all${RST})

  ${BLD}TEST${RST}
      Select individual test cases to run (e.g. ${BLD}posix/basic${RST}, ${BLD}"*exec*"${RST}, ...)
EOF
}

PATTERNS=()
SUDO=()
STOP=0
CLEAN=1
UPDATE=0
VERBOSE_COMMANDS=0
VERBOSE_ERRORS=0
VERBOSE_SKIPPED=0
VERBOSE_TESTS=0

for arg; do
    case "$arg" in
        --bfs=*)
            BFS="${arg#*=}"
            ;;
        --posix)
            PATTERNS+=("posix/*")
            ;;
        --bsd)
            PATTERNS+=("posix/*" "common/*" "bsd/*")
            ;;
        --gnu)
            PATTERNS+=("posix/*" "common/*" "gnu/*")
            ;;
        --all)
            PATTERNS+=("*")
            ;;
        --sudo)
            SUDO=(sudo)
            ;;
        --sudo=*)
            read -a SUDO <<<"${arg#*=}"
            ;;
        --stop)
            STOP=1
            ;;
        --no-clean|--noclean)
            CLEAN=0
            ;;
        --update)
            UPDATE=1
            ;;
        --verbose=commands)
            VERBOSE_COMMANDS=1
            ;;
        --verbose=errors)
            VERBOSE_ERRORS=1
            ;;
        --verbose=skipped)
            VERBOSE_SKIPPED=1
            ;;
        --verbose=tests)
            VERBOSE_TESTS=1
            ;;
        --verbose)
            VERBOSE_COMMANDS=1
            VERBOSE_ERRORS=1
            VERBOSE_SKIPPED=1
            VERBOSE_TESTS=1
            ;;
        --help)
            usage
            exit 0
            ;;
        -*)
            cprintf "${RED}error:${RST} Unrecognized option '%s'.\n\n" "$arg" >&2
            usage >&2
            exit 1
            ;;
        *)
            PATTERNS+=("$arg")
            ;;
    esac
done

function _realpath() {
    (
        cd "$(dirname -- "$1")"
        echo "$PWD/$(basename -- "$1")"
    )
}

TESTS=$(_realpath "$(dirname -- "${BASH_SOURCE[0]}")")

if [ "${BUILDDIR-}" ]; then
    BIN=$(_realpath "$BUILDDIR/bin")
else
    BIN=$(_realpath "$TESTS/../bin")
fi
MKSOCK="$BIN/tests/mksock"
XTOUCH="$BIN/tests/xtouch"

# Try to resolve the path to $BFS before we cd, while also supporting
# --bfs="./bin/bfs -S ids"
read -a BFS <<<"${BFS:-$BIN/bfs}"
BFS[0]=$(_realpath "$(command -v "${BFS[0]}")")

# The temporary directory that will hold our test data
TMP=$(mktemp -d "${TMPDIR:-/tmp}"/bfs.XXXXXXXXXX)
chown "$(id -u):$(id -g)" "$TMP"

cd "$TESTS"

if ((${#PATTERNS[@]} == 0)); then
    PATTERNS=("*")
fi

TEST_CASES=()
for TEST in {posix,common,bsd,gnu,bfs}/*.sh; do
    TEST="${TEST%.sh}"
    for PATTERN in "${PATTERNS[@]}"; do
        if [[ $TEST == $PATTERN ]]; then
            TEST_CASES+=("$TEST")
            break
        fi
    done
done

if ((${#TEST_CASES[@]} == 0)); then
    cprintf "${RED}error:${RST} No tests matched" >&2
    cprintf " ${BLD}%s${RST}" "${PATTERNS[@]}" >&2
    cprintf ".\n\n" >&2
    usage >&2
    exit 1
fi

function quote() {
    printf '%q' "$1"
    shift
    if (($# > 0)); then
        printf ' %q' "$@"
    fi
}

# Run a command when this (sub)shell exits
function defer() {
    trap -- KILL
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

function report_err() {
    local file="${1/#*\/tests\//tests\/}"
    set -- "$file" "${@:2}"

    if ((COLOR_STDERR)); then
        printf "${BLD}%s:%d:${RST} ${RED}error %d:${RST}\n    %s\n" "$@" >&2
    else
        printf "%s:%d: error %d:\n    %s\n" "$@" >&2
    fi
}

function pop_defer() {
    local cmd="${DEFER_CMDS[-1]}"
    local file="${DEFER_FILES[-1]}"
    local line="${DEFER_LINES[-1]}"
    unset "DEFER_CMDS[-1]"
    unset "DEFER_FILES[-1]"
    unset "DEFER_LINES[-1]"

    local ret=0
    eval "$cmd" || ret=$?

    if ((ret != 0)); then
        report_err "$file" $line $ret "defer $cmd"
    fi

    return $ret
}

function pop_defers() {
    local ret=0

    while ((${#DEFER_CMDS[@]} > 0)); do
        pop_defer || ret=$?
    done

    return $ret
}

function bfs_sudo() {
    if ((${#SUDO[@]})); then
        "${SUDO[@]}" "$@"
    else
        return 1
    fi
}

function clean_scratch() {
    if [ -e "$TMP/scratch" ]; then
        # Try to unmount anything left behind
        if ((${#SUDO[@]})) && command -v mountpoint &>/dev/null; then
            for path in "$TMP"/scratch/*; do
                if mountpoint -q "$path"; then
                    sudo umount "$path"
                fi
            done
        fi

        # Reset any modified permissions
        chmod -R +rX "$TMP/scratch"

        rm -rf "$TMP/scratch"
    fi

    mkdir "$TMP/scratch"
}

# Clean up temporary directories on exit
function cleanup() {
    # Don't force rm to deal with long paths
    for dir in "$TMP"/deep/*/*; do
        if [ -d "$dir" ]; then
            (cd "$dir" && rm -rf *)
        fi
    done

    # In case a test left anything weird in scratch/
    clean_scratch

    rm -rf "$TMP"
}

if ((CLEAN)); then
    defer cleanup
else
    echo "Test files saved to $TMP"
fi

# Creates a simple file+directory structure for tests
function make_basic() {
    "$XTOUCH" -p "$1"/{a,b,c/d,e/f,g/h/,i/}
    "$XTOUCH" -p "$1"/{j/foo,k/foo/bar,l/foo/bar/baz}
    echo baz >"$1/l/foo/bar/baz"
}
make_basic "$TMP/basic"

# Creates a file+directory structure with various permissions for tests
function make_perms() {
    "$XTOUCH" -p -M000 "$1/0"
    "$XTOUCH" -p -M444 "$1/r"
    "$XTOUCH" -p -M222 "$1/w"
    "$XTOUCH" -p -M644 "$1/rw"
    "$XTOUCH" -p -M555 "$1/rx"
    "$XTOUCH" -p -M311 "$1/wx"
    "$XTOUCH" -p -M755 "$1/rwx"
}
make_perms "$TMP/perms"

# Creates a file+directory structure with various symbolic and hard links
function make_links() {
    "$XTOUCH" -p "$1/file"
    ln -s file "$1/symlink"
    ln "$1/file" "$1/hardlink"
    ln -s nowhere "$1/broken"
    ln -s symlink/file "$1/notdir"
    "$XTOUCH" -p "$1/deeply/nested"/{dir/,file}
    ln -s file "$1/deeply/nested/link"
    ln -s nowhere "$1/deeply/nested/broken"
    ln -s deeply/nested "$1/skip"
}
make_links "$TMP/links"

# Creates a file+directory structure with symbolic link loops
function make_loops() {
    "$XTOUCH" -p "$1/file"
    ln -s file "$1/symlink"
    ln -s nowhere "$1/broken"
    ln -s symlink/file "$1/notdir"
    ln -s loop "$1/loop"
    mkdir -p "$1/deeply/nested/dir"
    ln -s ../../deeply "$1/deeply/nested/loop"
    ln -s deeply/nested/loop/nested "$1/skip"
}
make_loops "$TMP/loops"

# Creates a file+directory structure with varying timestamps
function make_times() {
    "$XTOUCH" -p -t "1991-12-14 00:00" "$1/a"
    "$XTOUCH" -p -t "1991-12-14 00:01" "$1/b"
    "$XTOUCH" -p -t "1991-12-14 00:02" "$1/c"
    ln -s a "$1/l"
    "$XTOUCH" -p -h -t "1991-12-14 00:03" "$1/l"
    "$XTOUCH" -p -t "1991-12-14 00:04" "$1"
}
make_times "$TMP/times"

# Creates a file+directory structure with various weird file/directory names
function make_weirdnames() {
    "$XTOUCH" -p "$1/-/a"
    "$XTOUCH" -p "$1/(/b"
    "$XTOUCH" -p "$1/(-/c"
    "$XTOUCH" -p "$1/!/d"
    "$XTOUCH" -p "$1/!-/e"
    "$XTOUCH" -p "$1/,/f"
    "$XTOUCH" -p "$1/)/g"
    "$XTOUCH" -p "$1/.../h"
    "$XTOUCH" -p "$1/\\/i"
    "$XTOUCH" -p "$1/ /j"
    "$XTOUCH" -p "$1/[/k"
}
make_weirdnames "$TMP/weirdnames"

# Creates a very deep directory structure for testing PATH_MAX handling
function make_deep() {
    mkdir -p "$1"

    # $name will be 255 characters, aka _XOPEN_NAME_MAX
    local name="0123456789ABCDEF"
    name="${name}${name}${name}${name}"
    name="${name}${name}${name}${name}"
    name="${name:0:255}"

    for i in {0..9} A B C D E F; do
        "$XTOUCH" -p "$1/$i/$name"

        (
            cd "$1/$i"

            # 8 * 512 == 4096 >= PATH_MAX
            for _ in {1..8}; do
                mv "$name" ..
                mkdir -p "$name/$name"
                mv "../$name" "$name/$name/"
            done
        )
    done
}
make_deep "$TMP/deep"

# Creates a directory structure with many different types, and therefore colors
function make_rainbow() {
    "$XTOUCH" -p "$1/file.txt"
    "$XTOUCH" -p "$1/file.dat"
    "$XTOUCH" -p "$1/lower".{gz,tar,tar.gz}
    "$XTOUCH" -p "$1/upper".{GZ,TAR,TAR.GZ}
    "$XTOUCH" -p "$1/lu.tar.GZ" "$1/ul.TAR.gz"
    ln -s file.txt "$1/link.txt"
    "$XTOUCH" -p "$1/mh1"
    ln "$1/mh1" "$1/mh2"
    mkfifo "$1/pipe"
    # TODO: block
    ln -s /dev/null "$1/chardev_link"
    ln -s nowhere "$1/broken"
    "$MKSOCK" "$1/socket"
    "$XTOUCH" -p "$1"/s{u,g,ug}id
    chmod u+s "$1"/su{,g}id
    chmod g+s "$1"/s{u,}gid
    mkdir "$1/ow" "$1"/sticky{,_ow}
    chmod o+w "$1"/*ow
    chmod +t "$1"/sticky*
    "$XTOUCH" -p "$1"/exec.sh
    chmod +x "$1"/exec.sh
    "$XTOUCH" -p "$1/"$'\e[1m/\e[0m'
}
make_rainbow "$TMP/rainbow"

mkdir "$TMP/scratch"

# Close stdin so bfs doesn't think we're interactive
exec </dev/null

if ((VERBOSE_COMMANDS)); then
    # dup stdout for verbose logging even when redirected
    exec 3>&1
fi

function bfs_verbose() {
    if ((!VERBOSE_COMMANDS)); then
        return
    fi

    {
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
    } | color >&3
}

function invoke_bfs() {
    bfs_verbose "$@"

    local ret=0
    "${BFS[@]}" "$@" || ret=$?

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

function bfs_pty() {
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

function check_exit() {
    local expected="$1"
    local actual="0"
    shift
    "$@" || actual="$?"
    ((actual == expected))
}

# Detect colored diff support
if ((COLOR_STDERR)) && diff --color=always /dev/null /dev/null 2>/dev/null; then
    DIFF="diff --color=always"
else
    DIFF="diff"
fi

# Return value when a difference is detected
EX_DIFF=20
# Return value when a test is skipped
EX_SKIP=77

function sort_output() {
    sort -o "$OUT" "$OUT"
}

function diff_output() {
    local GOLD="$TESTS/$TEST.out"

    if ((UPDATE)); then
        cp "$OUT" "$GOLD"
    else
        $DIFF -u "$GOLD" "$OUT" >&2
    fi
}

function bfs_diff() (
    bfs_verbose "$@"

    # Close the dup()'d stdout to make sure we have enough fd's for the process
    # substitution, even with low ulimit -n
    exec 3>&-

    "${BFS[@]}" "$@" | sort >"$OUT"
    local status="${PIPESTATUS[0]}"

    diff_output || exit $EX_DIFF
    return "$status"
)

function skip() {
    if ((VERBOSE_SKIPPED)); then
        caller | {
            read -r line file
            cprintf "${BOL}${CYN}%s skipped!${RST} (%s)\n" "$TEST" "$(awk "NR == $line" "$file")"
        }
    elif ((VERBOSE_TESTS)); then
        cprintf "${BOL}${CYN}%s skipped!${RST}\n" "$TEST"
    fi

    exit $EX_SKIP
}

function closefrom() {
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
        if ((fd >= $1)); then
            eval "exec ${fd}<&-"
        fi
    done
}

function inum() {
    ls -id "$@" | awk '{ print $1 }'
}

function set_acl() {
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

function make_xattrs() {
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

cd "$TMP"
set +e

BOL='\n'
EOL='\n'

function update_eol() {
    # Bash gets $COLUMNS from stderr, so if it's redirected use tput instead
    local cols="${COLUMNS-}"
    if [ -z "$cols" ]; then
        cols=$(tput cols)
    fi

    # Put the cursor at the last column, then write a space so the next
    # character will wrap
    EOL="\\033[${cols}G "
}

if ((VERBOSE_TESTS)); then
    BOL=''
elif ((COLOR_STDOUT)); then
    BOL='\r\033[K'

    # Workaround for bash 4: checkwinsize is off by default.  We can turn it on,
    # but we also have to explicitly trigger a foreground job to finish so that
    # it will update the window size before we use $COLUMNS
    shopt -s checkwinsize
    (:)

    update_eol
    trap update_eol WINCH
fi

function callers() {
    local frame=0
    while caller $frame; do
        ((++frame))
    done
}

function debug_err() {
    local ret=$? line func file
    callers | while read -r line func file; do
        if [ "$func" = source ]; then
            local cmd="$(awk "NR == $line" "$file" 2>/dev/null)" || :
            report_err "$file" $line $ret "$cmd"
            break
        fi
    done
}

function run_test() (
    set -eE
    trap debug_err ERR
    source "$@"
)

passed=0
failed=0
skipped=0

if ((COLOR_STDOUT || VERBOSE_TESTS)); then
    TEST_FMT="${BOL}${YLW}%s${RST}${EOL}"
else
    TEST_FMT="."
fi

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
        cprintf "${BOL}${RED}%s failed!${RST}\n" "$TEST"
        ((STOP)) && break
    fi
done

printf "${BOL}"

if ((passed > 0)); then
    cprintf "${GRN}tests passed: %d${RST}\n" "$passed"
fi
if ((skipped > 0)); then
    cprintf "${CYN}tests skipped: %s${RST}\n" "$skipped"
fi
if ((failed > 0)); then
    cprintf "${RED}tests failed: %s${RST}\n" "$failed"
    exit 1
fi
