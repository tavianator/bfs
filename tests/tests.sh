#!/usr/bin/env bash

############################################################################
# bfs                                                                      #
# Copyright (C) 2015-2022 Tavian Barnes <tavianator@tavianator.com>        #
#                                                                          #
# Permission to use, copy, modify, and/or distribute this software for any #
# purpose with or without fee is hereby granted.                           #
#                                                                          #
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES #
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF         #
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR  #
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES   #
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN    #
# ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF  #
# OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.           #
############################################################################

set -euP
umask 022

export LC_ALL=C
export TZ=UTC0

export ASAN_OPTIONS="abort_on_error=1:log_to_syslog=0"
export LSAN_OPTIONS="abort_on_error=1:log_to_syslog=0"
export MSAN_OPTIONS="abort_on_error=1:log_to_syslog=0"
export TSAN_OPTIONS="abort_on_error=1:log_to_syslog=0"
export UBSAN_OPTIONS="abort_on_error=1:log_to_syslog=0"

export LS_COLORS=""
unset BFS_COLORS

if [ -t 1 ]; then
    BLD=$'\033[01m'
    RED=$'\033[01;31m'
    GRN=$'\033[01;32m'
    YLW=$'\033[01;33m'
    BLU=$'\033[01;34m'
    MAG=$'\033[01;35m'
    CYN=$'\033[01;36m'
    RST=$'\033[0m'
else
    BLD=
    RED=
    GRN=
    YLW=
    BLU=
    MAG=
    CYN=
    RST=
fi

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
            cat >&2 <<EOF
${RED}error:${RST} Failed to drop capabilities.
EOF

	    exit 1
	fi

        cat >&2 <<EOF
${YLW}warning:${RST} Running as ${BLD}$(id -un)${RST} is not recommended.  Dropping ${BLD}cap_dac_override${RST} and
${BLD}cap_dac_read_search${RST}.

EOF

        BFS_TRIED_DROP=y exec capsh \
            --drop=cap_dac_override,cap_dac_read_search \
            --caps=cap_dac_override,cap_dac_read_search-eip \
            -- "$0" "$@"
    fi
elif [ "$EUID" -eq 0 ]; then
    UNLESS=
    if [ "$UNAME" = "Linux" ]; then
	UNLESS=" unless ${GRN}capsh${RST} is installed"
    fi

    cat >&2 <<EOF
${RED}error:${RST} These tests expect filesystem permissions to be enforced, and therefore
will not work when run as ${BLD}$(id -un)${RST}${UNLESS}.
EOF
    exit 1
fi

function usage() {
    local pad=$(printf "%*s" ${#0} "")
    cat <<EOF
Usage: ${GRN}$0${RST} [${BLU}--bfs${RST}=${MAG}path/to/bfs${RST}] [${BLU}--posix${RST}] [${BLU}--bsd${RST}] [${BLU}--gnu${RST}] [${BLU}--all${RST}] [${BLU}--sudo${RST}]
       $pad [${BLU}--stop${RST}] [${BLU}--noclean${RST}] [${BLU}--update${RST}] [${BLU}--verbose${RST}[=${BLD}LEVEL${RST}]] [${BLU}--help${RST}]
       $pad [${BLD}TEST${RST} [${BLD}TEST${RST} ...]]

  ${BLU}--bfs${RST}=${MAG}path/to/bfs${RST}
      Set the path to the bfs executable to test (default: ${MAG}./bin/bfs${RST})

  ${BLU}--posix${RST}, ${BLU}--bsd${RST}, ${BLU}--gnu${RST}, ${BLU}--all${RST}
      Choose which test cases to run (default: ${BLU}--all${RST})

  ${BLU}--sudo${RST}
      Run tests that require root

  ${BLU}--stop${RST}
      Stop when the first error occurs

  ${BLU}--noclean${RST}
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

  ${BLD}TEST${RST}
      Select individual test cases to run (e.g. ${BLD}posix/basic${RST}, ${BLD}"*exec*"${RST}, ...)
EOF
}

PATTERNS=()
SUDO=
STOP=
CLEAN=yes
UPDATE=
VERBOSE_COMMANDS=
VERBOSE_ERRORS=
VERBOSE_SKIPPED=
VERBOSE_TESTS=

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
            SUDO=yes
            ;;
        --stop)
            STOP=yes
            ;;
        --noclean)
            CLEAN=
            ;;
        --update)
            UPDATE=yes
            ;;
        --verbose=commands)
            VERBOSE_COMMANDS=yes
            ;;
        --verbose=errors)
            VERBOSE_ERRORS=yes
            ;;
        --verbose=skipped)
            VERBOSE_SKIPPED=yes
            ;;
        --verbose=tests)
            VERBOSE_SKIPPED=yes
            VERBOSE_TESTS=yes
            ;;
        --verbose)
            VERBOSE_COMMANDS=yes
            VERBOSE_ERRORS=yes
            VERBOSE_SKIPPED=yes
            VERBOSE_TESTS=yes
            ;;
        --help)
            usage
            exit 0
            ;;
        -*)
            printf "${RED}error:${RST} Unrecognized option '%s'.\n\n" "$arg" >&2
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

if (( ${#PATTERNS[@]} == 0 )); then
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

if (( ${#TEST_CASES[@]} == 0 )); then
    printf "${RED}error:${RST} No tests matched" >&2
    printf " ${BLD}%s${RST}" "${PATTERNS[@]}" >&2
    printf ".\n\n" >&2
    usage >&2
    exit 1
fi

function clean_scratch() {
    if [ -e "$TMP/scratch" ]; then
        # Try to unmount anything left behind
        if [ "$SUDO" ] && command -v mountpoint &>/dev/null; then
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

if [ "$CLEAN" ]; then
    trap cleanup EXIT
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

    # 4 * 256 - 1 == 1023
    local names="$name/$name/$name/$name"

    for i in {0..9} A B C D E F; do
        (
            mkdir "$1/$i"
            cd "$1/$i"

            # 4 * 1024 == 4096 == PATH_MAX
            for _ in {1..4}; do
                mkdir -p "$names"
                cd "$names"
            done

            "$XTOUCH" "$name"
        )
    done
}
make_deep "$TMP/deep"

# Creates a directory structure with many different types, and therefore colors
function make_rainbow() {
    "$XTOUCH" -p "$1/file.txt"
    "$XTOUCH" -p "$1/file.dat"
    "$XTOUCH" -p "$1/star".{gz,tar,tar.gz}
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
}
make_rainbow "$TMP/rainbow"

# Close stdin so bfs doesn't think we're interactive
exec </dev/null

if [ "$VERBOSE_COMMANDS" ]; then
    # dup stdout for verbose logging even when redirected
    exec 3>&1
fi

function bfs_verbose() {
    if [ "$VERBOSE_COMMANDS" ]; then
        if [ -t 3 ]; then
            printf "${GRN}%q${RST} " "${BFS[@]}" >&3

            local expr_started=
            for arg; do
                if [[ $arg == -[A-Z]* ]]; then
                    printf "${CYN}%q${RST} " "$arg" >&3
                elif [[ $arg == [\(!] || $arg == -[ao] || $arg == -and || $arg == -or || $arg == -not ]]; then
                    expr_started=yes
                    printf "${RED}%q${RST} " "$arg" >&3
                elif [[ $expr_started && $arg == [\),] ]]; then
                    printf "${RED}%q${RST} " "$arg" >&3
                elif [[ $arg == -?* ]]; then
                    expr_started=yes
                    printf "${BLU}%q${RST} " "$arg" >&3
                elif [ "$expr_started" ]; then
                    printf "${BLD}%q${RST} " "$arg" >&3
                else
                    printf "${MAG}%q${RST} " "$arg" >&3
                fi
            done
        else
            printf '%q ' "${BFS[@]}" "$@" >&3
        fi
        printf '\n' >&3
    fi
}

function invoke_bfs() {
    bfs_verbose "$@"
    "${BFS[@]}" "$@"
}

# Expect a command to fail, but not crash
function fail() {
    "$@"
    local STATUS="$?"

    if ((STATUS > 125)); then
        exit "$STATUS"
    elif ((STATUS > 0)); then
        return 0
    else
        return 1
    fi
}

# Detect colored diff support
if [ -t 2 ] && diff --color=always /dev/null /dev/null 2>/dev/null; then
    DIFF="diff --color=always"
else
    DIFF="diff"
fi

# Return value when bfs fails
EX_BFS=10
# Return value when a difference is detected
EX_DIFF=20
# Return value when a test is skipped
EX_SKIP=77

function sort_output() {
    sort -o "$OUT" "$OUT"
}

function diff_output() {
    local GOLD="$TESTS/$TEST.out"

    if [ "$UPDATE" ]; then
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
    local STATUS="${PIPESTATUS[0]}"

    diff_output || return $EX_DIFF

    if [ "$STATUS" -eq 0 ]; then
        return 0
    else
        return $EX_BFS
    fi
)

function skip() {
    exit $EX_SKIP
}

function skip_if() {
    if "$@"; then
        skip
    fi
}

function skip_unless() {
    skip_if fail "$@"
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
        if [ "$fd" -ge "$1" ]; then
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
            if [ "$(getconf ACL_NFS4 "$1")" -gt 0 ]; then
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
            [ "$SUDO" ] \
                && sudo setfattr -n security.bfs_test scratch/xattr \
                && sudo setfattr -n security.bfs_test_2 scratch/xattr_2 \
                && sudo setfattr -h -n security.bfs_test scratch/xattr_link
            ;;
    esac
}

cd "$TMP"
set +e

BOL='\n'
EOL='\n'

function update_eol() {
    # Put the cursor at the last column, then write a space so the next
    # character will wrap
    EOL="\\033[${COLUMNS}G "
}

if [ "$VERBOSE_TESTS" ]; then
    BOL=''
elif [ -t 1 ]; then
    BOL='\r\033[K'

    # Workaround for bash 4: checkwinsize is off by default.  We can turn it on,
    # but we also have to explicitly trigger a foreground job to finish so that
    # it will update the window size before we use $COLUMNS
    shopt -s checkwinsize
    (:)

    update_eol
    trap update_eol WINCH
fi

passed=0
failed=0
skipped=0

for TEST in "${TEST_CASES[@]}"; do
    if [[ -t 1 || "$VERBOSE_TESTS" ]]; then
        printf "${BOL}${YLW}%s${RST}${EOL}" "$TEST"
    else
        printf "."
    fi

    OUT="$TMP/$TEST.out"
    mkdir -p "${OUT%/*}"

    if [ "$VERBOSE_ERRORS" ]; then
        (. "$TESTS/$TEST.sh")
    else
        (. "$TESTS/$TEST.sh") 2>"$TMP/stderr"
    fi
    status=$?

    if ((status == 0)); then
        ((++passed))
    elif ((status == EX_SKIP)); then
        ((++skipped))
        if [ "$VERBOSE_SKIPPED" ]; then
            printf "${BOL}${CYN}%s skipped!${RST}\n" "$TEST"
        fi
    else
        ((++failed))
        [ "$VERBOSE_ERRORS" ] || cat "$TMP/stderr" >&2
        printf "${BOL}${RED}%s failed!${RST}\n" "$TEST"
        [ "$STOP" ] && break
    fi
done

printf "${BOL}"

if ((passed > 0)); then
    printf "${GRN}tests passed: %d${RST}\n" "$passed"
fi
if ((skipped > 0)); then
    printf "${CYN}tests skipped: %s${RST}\n" "$skipped"
fi
if ((failed > 0)); then
    printf "${RED}tests failed: %s${RST}\n" "$failed"
    exit 1
fi
