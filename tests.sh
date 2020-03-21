#!/bin/bash

############################################################################
# bfs                                                                      #
# Copyright (C) 2015-2020 Tavian Barnes <tavianator@tavianator.com>        #
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

set -e
set -o physical
umask 022

export LC_ALL=C
export TZ=UTC

if [ -t 1 ]; then
    BLD="$(printf '\033[01m')"
    RED="$(printf '\033[01;31m')"
    GRN="$(printf '\033[01;32m')"
    YLW="$(printf '\033[01;33m')"
    BLU="$(printf '\033[01;34m')"
    MAG="$(printf '\033[01;35m')"
    CYN="$(printf '\033[01;36m')"
    RST="$(printf '\033[0m')"
fi

if [ "$EUID" -eq 0 ]; then
    cat >&2 <<EOF
${RED}error:${RST} These tests expect filesystem permissions to be enforced, and therefore
will not work when run as ${BLD}$(id -un)${RST}.
EOF
    exit 1
fi

function usage() {
    local pad=$(printf "%*s" ${#0} "")
    cat <<EOF
Usage: ${GRN}$0${RST} [${BLU}--bfs${RST}=${MAG}path/to/bfs${RST}] [${BLU}--posix${RST}] [${BLU}--bsd${RST}] [${BLU}--gnu${RST}] [${BLU}--all${RST}] [${BLU}--sudo${RST}]
       $pad [${BLU}--noclean${RST}] [${BLU}--update${RST}] [${BLU}--verbose${RST}] [${BLU}--help${RST}]
       $pad [${BLD}test_*${RST} [${BLD}test_*${RST} ...]]

  ${BLU}--bfs${RST}=${MAG}path/to/bfs${RST}
      Set the path to the bfs executable to test (default: ${MAG}./bfs${RST})

  ${BLU}--posix${RST}, ${BLU}--bsd${RST}, ${BLU}--gnu${RST}, ${BLU}--all${RST}
      Choose which test cases to run (default: ${BLU}--all${RST})

  ${BLU}--sudo${RST}
      Run tests that require root (not included in ${BLU}--all${RST})

  ${BLU}--noclean${RST}
      Keep the test directories around after the run

  ${BLU}--update${RST}
      Update the expected outputs for the test cases

  ${BLU}--verbose${RST}
      Log the commands that get executed

  ${BLU}--help${RST}
      This message

  ${BLD}test_*${RST}
      Select individual test cases to run
EOF
}

function _realpath() {
    (
        cd "${1%/*}"
        echo "$PWD/${1##*/}"
    )
}

BFS="$(_realpath ./bfs)"
TESTS="$(_realpath ./tests)"
UNAME="$(uname)"

DEFAULT=yes
POSIX=
BSD=
GNU=
ALL=
SUDO=
CLEAN=yes
UPDATE=
VERBOSE=
EXPLICIT=

enabled_tests=()

for arg; do
    case "$arg" in
        --bfs=*)
            BFS="${arg#*=}"
            ;;
        --posix)
            DEFAULT=
            POSIX=yes
            ;;
        --bsd)
            DEFAULT=
            POSIX=yes
            BSD=yes
            ;;
        --gnu)
            DEFAULT=
            POSIX=yes
            GNU=yes
            ;;
        --all)
            DEFAULT=
            POSIX=yes
            BSD=yes
            GNU=yes
            ALL=yes
            ;;
        --sudo)
            DEFAULT=
            SUDO=yes
            ;;
        --noclean)
            CLEAN=
            ;;
        --update)
            UPDATE=yes
            ;;
        --verbose)
            VERBOSE=yes
            ;;
        --help)
            usage
            exit 0
            ;;
        test_*)
            EXPLICIT=yes
            enabled_tests+=("$arg")
            ;;
        *)
            printf "${RED}error:${RST} Unrecognized option '%s'.\n\n" "$arg" >&2
            usage >&2
            exit 1
            ;;
    esac
done

posix_tests=(
    # General parsing
    test_basic

    test_parens
    test_bang
    test_implicit_and
    test_a
    test_o

    test_weird_names

    # Flags

    test_H
    test_H_slash
    test_H_broken
    test_H_notdir
    test_H_loops

    test_L
    test_L_broken
    test_L_notdir
    test_L_loops

    test_flag_weird_names
    test_flag_comma

    # Primaries

    test_depth
    test_depth_slash
    test_depth_error
    test_L_depth

    test_exec
    test_exec_plus
    test_exec_plus_status
    test_exec_plus_semicolon

    test_group_name
    test_group_id

    test_links
    test_links_plus
    test_links_minus

    test_name
    test_name_root
    test_name_root_depth
    test_name_trailing_slash

    test_newer
    test_newer_link

    test_ok_stdin
    test_ok_plus_semicolon

    test_path

    test_perm_000
    test_perm_000_minus
    test_perm_222
    test_perm_222_minus
    test_perm_644
    test_perm_644_minus
    test_perm_symbolic
    test_perm_symbolic_minus
    test_perm_leading_plus_symbolic_minus
    test_permcopy

    test_prune
    test_prune_or_print
    test_not_prune

    test_size
    test_size_plus
    test_size_bytes

    test_type_d
    test_type_f

    test_user_name
    test_user_id

    # Closed file descriptors
    test_closed_stdin
    test_closed_stdout
    test_closed_stderr

    # PATH_MAX handling
    test_deep

    # Optimizer tests
    test_or_purity
    test_double_negation
    test_de_morgan_not
    test_de_morgan_and
    test_de_morgan_or
    test_data_flow_type
    test_data_flow_and_swap
    test_data_flow_or_swap
)

bsd_tests=(
    # Flags

    test_E

    test_P
    test_P_slash

    test_X

    test_d_path

    test_f

    test_s

    test_double_dash
    test_flag_double_dash

    # Primaries

    test_acl
    test_L_acl

    test_anewer
    test_asince

    test_delete

    test_depth_maxdepth_1
    test_depth_maxdepth_2
    test_depth_mindepth_1
    test_depth_mindepth_2

    test_depth_n
    test_depth_n_plus
    test_depth_n_minus
    test_depth_depth_n
    test_depth_depth_n_plus
    test_depth_depth_n_minus
    test_depth_overflow
    test_data_flow_depth

    test_exec_substring

    test_execdir_pwd
    test_execdir_slash
    test_execdir_slash_pwd
    test_execdir_slashes
    test_execdir_ulimit

    test_exit

    test_follow

    test_gid_name

    test_ilname
    test_L_ilname

    test_iname

    test_inum

    test_ipath

    test_iregex

    test_lname
    test_L_lname

    test_maxdepth

    test_mindepth

    test_mnewer
    test_H_mnewer

    test_msince

    test_name_slash
    test_name_slashes

    test_H_newer

    test_newerma
    test_newermt
    test_newermt_epoch_minus_one

    test_nogroup
    test_nogroup_ulimit

    test_nouser
    test_nouser_ulimit

    test_ok_stdin
    test_ok_closed_stdin

    test_okdir_stdin
    test_okdir_closed_stdin

    test_perm_000_plus
    test_perm_222_plus
    test_perm_644_plus

    test_printx

    test_quit
    test_quit_child
    test_quit_depth
    test_quit_depth_child
    test_quit_after_print
    test_quit_before_print
    test_quit_implicit_print

    test_rm

    test_regex
    test_regex_parens

    test_samefile
    test_samefile_symlink
    test_H_samefile_symlink
    test_L_samefile_symlink
    test_samefile_broken
    test_H_samefile_broken
    test_L_samefile_broken
    test_samefile_notdir
    test_H_samefile_notdir
    test_L_samefile_notdir

    test_size_T
    test_size_big

    test_uid_name
)

gnu_tests=(
    # General parsing

    test_not
    test_and
    test_or
    test_comma
    test_precedence

    test_follow_comma

    # Flags

    test_P
    test_P_slash

    test_L_loops_continue

    test_double_dash
    test_flag_double_dash

    # Primaries

    test_anewer

    test_path_d

    test_daystart
    test_daystart_twice

    test_delete
    test_L_delete

    test_depth_mindepth_1
    test_depth_mindepth_2
    test_depth_maxdepth_1
    test_depth_maxdepth_2

    test_empty
    test_empty_special

    test_exec_nothing
    test_exec_substring

    test_execdir
    test_execdir_substring
    test_execdir_plus_semicolon
    test_execdir_pwd
    test_execdir_slash
    test_execdir_slash_pwd
    test_execdir_slashes
    test_execdir_ulimit

    test_executable

    test_false

    test_follow

    test_fprint
    test_fprint_duplicate
    test_fprint_error

    test_fstype

    test_gid
    test_gid_plus
    test_gid_plus_plus
    test_gid_minus
    test_gid_minus_plus

    test_ignore_readdir_race
    test_ignore_readdir_race_root
    test_ignore_readdir_race_notdir

    test_ilname
    test_L_ilname

    test_iname

    test_inum

    test_ipath

    test_iregex

    test_lname
    test_L_lname

    test_maxdepth

    test_mindepth

    test_name_slash
    test_name_slashes

    test_H_newer

    test_newerma
    test_newermt
    test_newermt_epoch_minus_one

    test_nogroup
    test_nogroup_ulimit

    test_nouser
    test_nouser_ulimit

    test_ok_closed_stdin
    test_ok_nothing

    test_okdir_closed_stdin
    test_okdir_plus_semicolon

    test_perm_000_slash
    test_perm_222_slash
    test_perm_644_slash
    test_perm_symbolic_slash
    test_perm_leading_plus_symbolic_slash

    test_print_error

    test_printf
    test_printf_empty
    test_printf_slash
    test_printf_slashes
    test_printf_trailing_slash
    test_printf_trailing_slashes
    test_printf_flags
    test_printf_types
    test_printf_escapes
    test_printf_times
    test_printf_leak
    test_printf_nul
    test_printf_Y_error
    test_printf_H
    test_printf_u_g_ulimit

    test_quit
    test_quit_child
    test_quit_depth
    test_quit_depth_child
    test_quit_after_print
    test_quit_before_print

    test_readable

    test_regex
    test_regex_parens

    test_regextype_posix_basic
    test_regextype_posix_extended

    test_samefile
    test_samefile_symlink
    test_H_samefile_symlink
    test_L_samefile_symlink
    test_samefile_broken
    test_H_samefile_broken
    test_L_samefile_broken
    test_samefile_notdir
    test_H_samefile_notdir
    test_L_samefile_notdir

    test_size_big

    test_true

    test_uid
    test_uid_plus
    test_uid_plus_plus
    test_uid_minus
    test_uid_minus_plus

    test_writable

    test_xtype_l
    test_xtype_f
    test_L_xtype_l
    test_L_xtype_f

    # Optimizer tests
    test_and_purity
    test_not_reachability
    test_comma_reachability
    test_and_false_or_true
    test_comma_redundant_true
    test_comma_redundant_false
)

bfs_tests=(
    # General parsing
    test_path_flag_expr
    test_path_expr_flag
    test_flag_expr_path
    test_expr_flag_path
    test_expr_path_flag

    # Flags

    test_S_bfs
    test_S_dfs
    test_S_ids

    # Primaries

    test_color
    test_color_L
    test_color_rs_lc_rc_ec
    test_color_escapes
    test_color_nul
    test_color_ln_target
    test_color_L_ln_target
    test_color_mh
    test_color_mh0
    test_color_or
    test_color_mi
    test_color_or_mi
    test_color_or_mi0
    test_color_or0_mi
    test_color_or0_mi0
    test_color_su_sg0
    test_color_su0_sg
    test_color_su0_sg0
    test_color_st_tw_ow0
    test_color_st_tw0_ow
    test_color_st_tw0_ow0
    test_color_st0_tw_ow
    test_color_st0_tw_ow0
    test_color_st0_tw0_ow
    test_color_st0_tw0_ow0
    test_color_ext
    test_color_ext0
    test_color_ext_override
    test_color_ext_underride
    test_color_missing_colon
    test_color_no_stat
    test_color_L_no_stat
    test_color_star
    test_color_ls

    test_execdir_plus

    test_help

    test_hidden

    test_nohidden

    test_perm_symbolic_trailing_comma
    test_perm_symbolic_double_comma
    test_perm_symbolic_missing_action
    test_perm_leading_plus_symbolic

    test_printf_w

    test_type_multi

    test_unique
    test_unique_depth
    test_L_unique
    test_L_unique_loops
    test_L_unique_depth

    test_xtype_multi
    test_xtype_reorder

    # PATH_MAX handling
    test_deep_strict
)

sudo_tests=(
    test_capable
    test_L_capable

    test_mount
    test_L_mount
    test_xdev
    test_L_xdev

    test_inum_mount
    test_inum_bind_mount
    test_type_bind_mount
    test_xtype_bind_mount
)

case "$UNAME" in
    Darwin|FreeBSD)
        bsd_tests+=(
            test_xattr
            test_L_xattr
        )
        ;;
    *)
        sudo_tests+=(
            test_xattr
            test_L_xattr
        )
        ;;
esac

if [ "$DEFAULT" ]; then
    POSIX=yes
    BSD=yes
    GNU=yes
    ALL=yes
fi

if [ ! "$EXPLICIT" ]; then
    [ "$POSIX" ] && enabled_tests+=("${posix_tests[@]}")
    [ "$BSD" ] && enabled_tests+=("${bsd_tests[@]}")
    [ "$GNU" ] && enabled_tests+=("${gnu_tests[@]}")
    [ "$ALL" ] && enabled_tests+=("${bfs_tests[@]}")
    [ "$SUDO" ] && enabled_tests+=("${sudo_tests[@]}")
fi

eval enabled_tests=($(printf '%q\n' "${enabled_tests[@]}" | sort -u))

# The temporary directory that will hold our test data
TMP="$(mktemp -d "${TMPDIR:-/tmp}"/bfs.XXXXXXXXXX)"
chown "$(id -u):$(id -g)" "$TMP"

# Clean up temporary directories on exit
function cleanup() {
    # Don't force rm to deal with long paths
    for dir in "$TMP"/deep/*/*; do
        if [ -d "$dir" ]; then
            (cd "$dir" && rm -rf *)
        fi
    done

    # In case a test left anything weird in scratch/
    if [ -e "$TMP"/scratch ]; then
        chmod -R +rX "$TMP"/scratch
    fi

    rm -rf "$TMP"
}

if [ "$CLEAN" ]; then
    trap cleanup EXIT
else
    echo "Test files saved to $TMP"
fi

# Install a file, creating any parent directories
function installp() {
    local target="${@: -1}"
    mkdir -p "${target%/*}"
    install "$@"
}

# Like a mythical touch -p
function touchp() {
    for arg; do
        installp -m644 /dev/null "$arg"
    done
}

# Creates a simple file+directory structure for tests
function make_basic() {
    touchp "$1/a"
    touchp "$1/b"
    touchp "$1/c/d"
    touchp "$1/e/f"
    mkdir -p "$1/g/h"
    mkdir -p "$1/i"
    touchp "$1/j/foo"
    touchp "$1/k/foo/bar"
    touchp "$1/l/foo/bar/baz"
    echo baz >"$1/l/foo/bar/baz"
}
make_basic "$TMP/basic"

# Creates a file+directory structure with various permissions for tests
function make_perms() {
    installp -m000 /dev/null "$1/0"
    installp -m444 /dev/null "$1/r"
    installp -m222 /dev/null "$1/w"
    installp -m644 /dev/null "$1/rw"
    installp -m555 /dev/null "$1/rx"
    installp -m311 /dev/null "$1/wx"
    installp -m755 /dev/null "$1/rwx"
}
make_perms "$TMP/perms"

# Creates a file+directory structure with various symbolic and hard links
function make_links() {
    touchp "$1/file"
    ln -s file "$1/symlink"
    ln "$1/file" "$1/hardlink"
    ln -s nowhere "$1/broken"
    ln -s symlink/file "$1/notdir"
    mkdir -p "$1/deeply/nested/dir"
    touchp "$1/deeply/nested/file"
    ln -s file "$1/deeply/nested/link"
    ln -s deeply/nested "$1/skip"
}
make_links "$TMP/links"

# Creates a file+directory structure with symbolic link loops
function make_loops() {
    touchp "$1/file"
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
    mkdir -p "$1"
    touch -t 199112140000 "$1/a"
    touch -t 199112140001 "$1/b"
    touch -t 199112140002 "$1/c"
    ln -s a "$1/l"
    touch -h -t 199112140003 "$1/l"
    touch -t 199112140004 "$1"
}
make_times "$TMP/times"

# Creates a file+directory structure with various weird file/directory names
function make_weirdnames() {
    touchp "$1/-/a"
    touchp "$1/(/b"
    touchp "$1/(-/c"
    touchp "$1/!/d"
    touchp "$1/!-/e"
    touchp "$1/,/f"
    touchp "$1/)/g"
    touchp "$1/.../h"
    touchp "$1/\\/i"
    touchp "$1/ /j"
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
        (
            mkdir "$1/$i"
            cd "$1/$i"

            # 16 * 256 == 4096 == PATH_MAX
            for j in {1..16}; do
                mkdir "$name"
                cd "$name" 2>/dev/null
            done

            touch "$name"
        )
    done
}
make_deep "$TMP/deep"

# Creates a directory structure with many different types, and therefore colors
function make_rainbow() {
    touchp "$1/file.txt"
    touchp "$1/file.dat"
    touchp "$1/star".{gz,tar,tar.gz}
    ln -s file.txt "$1/link.txt"
    touchp "$1/mh1"
    ln "$1/mh1" "$1/mh2"
    mkfifo "$1/pipe"
    # TODO: block
    ln -s /dev/null "$1/chardev_link"
    ln -s nowhere "$1/broken"
    "$TESTS/mksock" "$1/socket"
    touchp "$1"/s{u,g,ug}id
    chmod u+s "$1"/su{,g}id
    chmod g+s "$1"/s{u,}gid
    mkdir "$1/ow" "$1"/sticky{,_ow}
    chmod o+w "$1"/*ow
    chmod +t "$1"/sticky*
    touchp "$1"/exec.sh
    chmod +x "$1"/exec.sh
}
make_rainbow "$TMP/rainbow"

# Creates a scratch directory that tests can modify
function make_scratch() {
    mkdir -p "$1"
}
make_scratch "$TMP/scratch"

function bfs_sort() {
    awk -F/ '{ print NF - 1 " " $0 }' | sort -n | cut -d' ' -f2-
}

# Close stdin so bfs doesn't think we're interactive
exec </dev/null

if [ "$VERBOSE" ]; then
    # dup stdout for verbose logging even when redirected
    exec 3>&1
fi

function bfs_verbose() {
    if [ "$VERBOSE" ]; then
        if [ -t 3 ]; then
            printf "${GRN}%q${RST} " $BFS >&3

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
            printf '%q ' $BFS "$@" >&3
        fi
        printf '\n' >&3
    fi
}

function invoke_bfs() {
    bfs_verbose "$@"
    $BFS "$@"
}

# Return value when bfs fails
EX_BFS=10
# Return value when a difference is detected
EX_DIFF=20

function bfs_diff() (
    bfs_verbose "$@"

    # Close the dup()'d stdout to make sure we have enough fd's for the process
    # substitution, even with low ulimit -n
    exec 3>&-

    local EXPECTED="$TESTS/${FUNCNAME[1]}.out"
    if [ "$UPDATE" ]; then
        local ACTUAL="$EXPECTED"
    else
        local ACTUAL="$TMP/${FUNCNAME[1]}.out"
    fi

    $BFS "$@" | bfs_sort >"$ACTUAL"
    local STATUS="${PIPESTATUS[0]}"

    if [ ! "$UPDATE" ]; then
        diff -u "$EXPECTED" "$ACTUAL" || return $EX_DIFF
    fi

    if [ "$STATUS" -eq 0 ]; then
        return 0
    else
        return $EX_BFS
    fi
)

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


cd "$TMP"
set +e

# Test cases

function test_basic() {
    bfs_diff basic
}

function test_type_d() {
    bfs_diff basic -type d
}

function test_type_f() {
    bfs_diff basic -type f
}

function test_type_multi() {
    bfs_diff links -type f,d,c
}

function test_mindepth() {
    bfs_diff basic -mindepth 1
}

function test_maxdepth() {
    bfs_diff basic -maxdepth 1
}

function test_depth() {
    bfs_diff basic -depth
}

function test_depth_slash() {
    bfs_diff basic/ -depth
}

function test_depth_mindepth_1() {
    bfs_diff basic -mindepth 1 -depth
}

function test_depth_mindepth_2() {
    bfs_diff basic -mindepth 2 -depth
}

function test_depth_maxdepth_1() {
    bfs_diff basic -maxdepth 1 -depth
}

function test_depth_maxdepth_2() {
    bfs_diff basic -maxdepth 2 -depth
}

function test_depth_error() {
    rm -rf scratch/*
    touchp scratch/foo/bar
    chmod -r scratch/foo

    bfs_diff scratch -depth 2>/dev/null
    local ret=$?

    chmod +r scratch/foo
    rm -rf scratch/*

    [ $ret -eq $EX_BFS ]
}

function test_name() {
    bfs_diff basic -name '*f*'
}

function test_name_root() {
    bfs_diff basic/a -name a
}

function test_name_root_depth() {
    bfs_diff basic/g -depth -name g
}

function test_name_trailing_slash() {
    bfs_diff basic/g/ -name g
}

function test_name_slash() {
    bfs_diff / -maxdepth 0 -name /
}

function test_name_slashes() {
    bfs_diff /// -maxdepth 0 -name /
}

function test_path() {
    bfs_diff basic -path 'basic/*f*'
}

function test_true() {
    bfs_diff basic -true
}

function test_false() {
    bfs_diff basic -false
}

function test_executable() {
    bfs_diff perms -executable
}

function test_readable() {
    bfs_diff perms -readable
}

function test_writable() {
    bfs_diff perms -writable
}

function test_empty() {
    bfs_diff basic -empty
}

function test_empty_special() {
    bfs_diff rainbow -empty
}

function test_gid() {
    bfs_diff basic -gid "$(id -g)"
}

function test_gid_plus() {
    bfs_diff basic -gid +0
}

function test_gid_plus_plus() {
    bfs_diff basic -gid +0
}

function test_gid_minus() {
    bfs_diff basic -gid "-$(($(id -g) + 1))"
}

function test_gid_minus_plus() {
    bfs_diff basic -gid "-+$(($(id -g) + 1))"
}

function test_uid() {
    bfs_diff basic -uid "$(id -u)"
}

function test_uid_plus() {
    bfs_diff basic -uid +0
}

function test_uid_plus_plus() {
    bfs_diff basic -uid ++0
}

function test_uid_minus() {
    bfs_diff basic -uid "-$(($(id -u) + 1))"
}

function test_uid_minus_plus() {
    bfs_diff basic -uid "-+$(($(id -u) + 1))"
}

function test_newer() {
    bfs_diff times -newer times/a
}

function test_newer_link() {
    bfs_diff times -newer times/l
}

function test_anewer() {
    bfs_diff times -anewer times/a
}

function test_asince() {
    bfs_diff times -asince 1991-12-14T00:01
}

function test_links() {
    bfs_diff links -type f -links 2
}

function test_links_plus() {
    bfs_diff links -type f -links +1
}

function test_links_minus() {
    bfs_diff links -type f -links -2
}

function test_P() {
    bfs_diff -P links/deeply/nested/dir
}

function test_P_slash() {
    bfs_diff -P links/deeply/nested/dir/
}

function test_H() {
    bfs_diff -H links/deeply/nested/dir
}

function test_H_slash() {
    bfs_diff -H links/deeply/nested/dir/
}

function test_H_broken() {
    bfs_diff -H links/broken
}

function test_H_notdir() {
    bfs_diff -H links/notdir
}

function test_H_newer() {
    bfs_diff -H times -newer times/l
}

function test_H_loops() {
    bfs_diff -H loops/deeply/nested/loop
}

function test_L() {
    bfs_diff -L links
}

function test_L_broken() {
    bfs_diff -H links/broken
}

function test_L_notdir() {
    bfs_diff -H links/notdir
}

function test_L_loops() {
    # POSIX says it's okay to either stop or keep going on seeing a filesystem
    # loop, as long as a diagnostic is printed
    local errors="$(invoke_bfs -L loops 2>&1 >/dev/null)"
    [ -n "$errors" ]
}

function test_L_loops_continue() {
    bfs_diff -L loops 2>/dev/null
    [ $? -eq $EX_BFS ]
}

function test_X() {
    bfs_diff -X weirdnames 2>/dev/null
    [ $? -eq $EX_BFS ]
}

function test_follow() {
    bfs_diff links -follow
}

function test_L_depth() {
    bfs_diff -L links -depth
}

function test_samefile() {
    bfs_diff links -samefile links/file
}

function test_samefile_symlink() {
    bfs_diff links -samefile links/symlink
}

function test_H_samefile_symlink() {
    bfs_diff -H links -samefile links/symlink
}

function test_L_samefile_symlink() {
    bfs_diff -L links -samefile links/symlink
}

function test_samefile_broken() {
    bfs_diff links -samefile links/broken
}

function test_H_samefile_broken() {
    bfs_diff -H links -samefile links/broken
}

function test_L_samefile_broken() {
    bfs_diff -L links -samefile links/broken
}

function test_samefile_notdir() {
    bfs_diff links -samefile links/notdir
}

function test_H_samefile_notdir() {
    bfs_diff -H links -samefile links/notdir
}

function test_L_samefile_notdir() {
    bfs_diff -L links -samefile links/notdir
}

function test_xtype_l() {
    bfs_diff links -xtype l
}

function test_xtype_f() {
    bfs_diff links -xtype f
}

function test_L_xtype_l() {
    bfs_diff -L links -xtype l
}

function test_L_xtype_f() {
    bfs_diff -L links -xtype f
}

function test_xtype_multi() {
    bfs_diff links -xtype f,d,c
}

function test_xtype_reorder() {
    # Make sure -xtype is not reordered in front of anything -- if -xtype runs
    # before -links 100, it will report an ELOOP error
    bfs_diff loops -links 100 -xtype l
    invoke_bfs loops -links 100 -xtype l
}

function test_iname() {
    bfs_diff basic -iname '*F*'
}

function test_ipath() {
    bfs_diff basic -ipath 'basic/*F*'
}

function test_lname() {
    bfs_diff links -lname '[aq]'
}

function test_ilname() {
    bfs_diff links -ilname '[AQ]'
}

function test_L_lname() {
    bfs_diff -L links -lname '[aq]'
}

function test_L_ilname() {
    bfs_diff -L links -ilname '[AQ]'
}

function test_user_name() {
    bfs_diff basic -user "$(id -un)"
}

function test_user_id() {
    bfs_diff basic -user "$(id -u)"
}

function test_group_name() {
    bfs_diff basic -group "$(id -gn)"
}

function test_group_id() {
    bfs_diff basic -group "$(id -g)"
}

function test_daystart() {
    bfs_diff basic -daystart -mtime 0
}

function test_daystart_twice() {
    bfs_diff basic -daystart -daystart -mtime 0
}

function test_newerma() {
    bfs_diff times -newerma times/a
}

function test_newermt() {
    bfs_diff times -newermt 1991-12-14T00:01
}

function test_newermt_epoch_minus_one() {
    bfs_diff times -newermt 1969-12-31T23:59:59Z
}

function test_size() {
    bfs_diff basic -type f -size 0
}

function test_size_plus() {
    bfs_diff basic -type f -size +0
}

function test_size_bytes() {
    bfs_diff basic -type f -size +0c
}

function test_size_big() {
    bfs_diff basic -size 9223372036854775807
}

function test_exec() {
    bfs_diff basic -exec echo '{}' \;
}

function test_exec_nothing() {
    # Regression test: don't segfault on missing command
    ! invoke_bfs basic -exec \; 2>/dev/null
}

function test_exec_plus() {
    bfs_diff basic -exec "$TESTS/sort-args.sh" '{}' +
}

function test_exec_plus_status() {
    # -exec ... {} + should always return true, but if the command fails, bfs
    # should exit with a non-zero status
    bfs_diff basic -exec false '{}' + -print
    ! invoke_bfs basic -exec false '{}' +
}

function test_exec_plus_semicolon() {
    # POSIX says:
    #     Only a <plus-sign> that immediately follows an argument containing only the two characters "{}"
    #     shall punctuate the end of the primary expression. Other uses of the <plus-sign> shall not be
    #     treated as special.
    bfs_diff basic -exec echo foo '{}' bar + baz \;
}

function test_exec_substring() {
    bfs_diff basic -exec echo '-{}-' ';'
}

function test_execdir() {
    bfs_diff basic -execdir echo '{}' ';'
}

function test_execdir_plus() {
    if [[ "$BFS" != *"-S dfs"* ]]; then
        bfs_diff basic -execdir "$TESTS/sort-args.sh" '{}' +
    fi
}

function test_execdir_substring() {
    bfs_diff basic -execdir echo '-{}-' ';'
}

function test_execdir_plus_semicolon() {
    bfs_diff basic -execdir echo foo '{}' bar + baz \;
}

function test_execdir_pwd() {
    local TMP_REAL="$(cd "$TMP" && pwd)"
    local OFFSET="$((${#TMP_REAL} + 2))"
    bfs_diff basic -execdir bash -c "pwd | cut -b$OFFSET-" ';'
}

function test_execdir_slash() {
    # Don't prepend ./ for absolute paths in -execdir
    bfs_diff / -maxdepth 0 -execdir echo '{}' ';'
}

function test_execdir_slash_pwd() {
    bfs_diff / -maxdepth 0 -execdir pwd ';'
}

function test_execdir_slashes() {
    bfs_diff /// -maxdepth 0 -execdir echo '{}' ';'
}

function test_execdir_ulimit() {
    rm -rf scratch/*
    mkdir -p scratch/a/b/c/d/e/f/g/h/i/j/k/l/m/n/o/p/q/r/s/t/u/v/w/x/y/z
    mkdir -p scratch/a/b/c/d/e/f/g/h/i/j/k/l/m/0/1/2/3/4/5/6/7/8/9/A/B/C

    closefrom 4
    ulimit -n 13
    bfs_diff scratch -execdir echo '{}' ';'
}

function test_weird_names() {
    cd weirdnames
    bfs_diff '-' '(-' '!-' ',' ')' './(' './!' \( \! -print -o -print \)
}

function test_flag_weird_names() {
    cd weirdnames
    bfs_diff -L '-' '(-' '!-' ',' ')' './(' './!' \( \! -print -o -print \)
}

function test_flag_comma() {
    # , is a filename until a non-flag is seen
    cd weirdnames
    bfs_diff -L ',' -print
}

function test_follow_comma() {
    # , is an operator after a non-flag is seen
    cd weirdnames
    bfs_diff -follow ',' -print
}

function test_fprint() {
    invoke_bfs basic -fprint scratch/test_fprint.out
    sort -o scratch/test_fprint.out scratch/test_fprint.out

    if [ "$UPDATE" ]; then
        cp scratch/test_fprint.out "$TESTS/test_fprint.out"
    else
        diff -u scratch/test_fprint.out "$TESTS/test_fprint.out"
    fi
}

function test_fprint_duplicate() {
    touchp scratch/test_fprint_duplicate.out
    ln scratch/test_fprint_duplicate.out scratch/test_fprint_duplicate.hard
    ln -s test_fprint_duplicate.out scratch/test_fprint_duplicate.soft

    invoke_bfs basic -fprint scratch/test_fprint_duplicate.out -fprint scratch/test_fprint_duplicate.hard -fprint scratch/test_fprint_duplicate.soft
    sort -o scratch/test_fprint_duplicate.out scratch/test_fprint_duplicate.out

    if [ "$UPDATE" ]; then
        cp scratch/test_fprint_duplicate.out "$TESTS/test_fprint_duplicate.out"
    else
        diff -u scratch/test_fprint_duplicate.out "$TESTS/test_fprint_duplicate.out"
    fi
}

function test_double_dash() {
    cd basic
    bfs_diff -- . -type f
}

function test_flag_double_dash() {
    cd basic
    bfs_diff -L -- . -type f
}

function test_ignore_readdir_race() {
    rm -rf scratch/*
    touch scratch/{foo,bar}

    # -links 1 forces a stat() call, which will fail for the second file
    invoke_bfs scratch -mindepth 1 -ignore_readdir_race -links 1 -exec "$TESTS/remove-sibling.sh" '{}' ';'
}

function test_ignore_readdir_race_root() {
    # Make sure -ignore_readdir_race doesn't suppress ENOENT at the root
    ! invoke_bfs basic/nonexistent -ignore_readdir_race 2>/dev/null
}

function test_ignore_readdir_race_notdir() {
    # Check -ignore_readdir_race handling when a directory is replaced with a file
    rm -rf scratch/*
    touchp scratch/foo/bar

    invoke_bfs scratch -mindepth 1 -ignore_readdir_race -execdir rm -r '{}' \; -execdir touch '{}' \;
}

function test_perm_000() {
    bfs_diff perms -perm 000
}

function test_perm_000_minus() {
    bfs_diff perms -perm -000
}

function test_perm_000_slash() {
    bfs_diff perms -perm /000
}

function test_perm_000_plus() {
    bfs_diff perms -perm +000
}

function test_perm_222() {
    bfs_diff perms -perm 222
}

function test_perm_222_minus() {
    bfs_diff perms -perm -222
}

function test_perm_222_slash() {
    bfs_diff perms -perm /222
}

function test_perm_222_plus() {
    bfs_diff perms -perm +222
}

function test_perm_644() {
    bfs_diff perms -perm 644
}

function test_perm_644_minus() {
    bfs_diff perms -perm -644
}

function test_perm_644_slash() {
    bfs_diff perms -perm /644
}

function test_perm_644_plus() {
    bfs_diff perms -perm +644
}

function test_perm_symbolic() {
    bfs_diff perms -perm a+r,u=wX,g+wX-w
}

function test_perm_symbolic_minus() {
    bfs_diff perms -perm -a+r,u=wX,g+wX-w
}

function test_perm_symbolic_slash() {
    bfs_diff perms -perm /a+r,u=wX,g+wX-w
}

function test_perm_symbolic_trailing_comma() {
    ! invoke_bfs perms -perm a+r, 2>/dev/null
}

function test_perm_symbolic_double_comma() {
    ! invoke_bfs perms -perm a+r,,u+w 2>/dev/null
}

function test_perm_symbolic_missing_action() {
    ! invoke_bfs perms -perm a 2>/dev/null
}

function test_perm_leading_plus_symbolic() {
    bfs_diff perms -perm +rwx
}

function test_perm_leading_plus_symbolic_minus() {
    bfs_diff perms -perm -+rwx
}

function test_perm_leading_plus_symbolic_slash() {
    bfs_diff perms -perm /+rwx
}

function test_permcopy() {
    bfs_diff perms -perm u+rw,g+u-w,o=g
}

function test_prune() {
    bfs_diff basic -name foo -prune
}

function test_prune_or_print() {
    bfs_diff basic -name foo -prune -o -print
}

function test_not_prune() {
    bfs_diff basic \! \( -name foo -prune \)
}

function test_ok_nothing() {
    # Regression test: don't segfault on missing command
    ! invoke_bfs basic -ok \; 2>/dev/null
}

function test_ok_stdin() {
    # -ok should *not* close stdin
    # See https://savannah.gnu.org/bugs/?24561
    yes | bfs_diff basic -ok bash -c 'printf "%s? " "$1" && head -n1' bash '{}' \; 2>/dev/null
}

function test_okdir_stdin() {
    # -okdir should *not* close stdin
    yes | bfs_diff basic -okdir bash -c 'printf "%s? " "$1" && head -n1' bash '{}' \; 2>/dev/null
}

function test_ok_plus_semicolon() {
    yes | bfs_diff basic -ok echo '{}' + \; 2>/dev/null
}

function test_okdir_plus_semicolon() {
    yes | bfs_diff basic -okdir echo '{}' + \; 2>/dev/null
}

function test_delete() {
    rm -rf scratch/*
    touchp scratch/foo/bar/baz

    # Don't try to delete '.'
    (cd scratch && invoke_bfs . -delete)

    bfs_diff scratch
}

function test_L_delete() {
    rm -rf scratch/*
    mkdir scratch/foo
    mkdir scratch/bar
    ln -s ../foo scratch/bar/baz

    # Don't try to rmdir() a symlink
    invoke_bfs -L scratch/bar -delete || return 1

    bfs_diff scratch
}

function test_rm() {
    rm -rf scratch/*
    touchp scratch/foo/bar/baz

    (cd scratch && invoke_bfs . -rm)

    bfs_diff scratch
}

function test_regex() {
    bfs_diff basic -regex 'basic/./.'
}

function test_iregex() {
    bfs_diff basic -iregex 'basic/[A-Z]/[a-z]'
}

function test_regex_parens() {
    cd weirdnames
    bfs_diff . -regex '\./\((\)'
}

function test_E() {
    cd weirdnames
    bfs_diff -E . -regex '\./(\()'
}

function test_regextype_posix_basic() {
    cd weirdnames
    bfs_diff -regextype posix-basic -regex '\./\((\)'
}

function test_regextype_posix_extended() {
    cd weirdnames
    bfs_diff -regextype posix-extended -regex '\./(\()'
}

function test_d_path() {
    bfs_diff -d basic
}

function test_path_d() {
    bfs_diff basic -d
}

function test_f() {
    cd weirdnames
    bfs_diff -f '-' -f '('
}

function test_s() {
    invoke_bfs -s weirdnames -maxdepth 1 >"$TMP/test_s.out"

    if [ "$UPDATE" ]; then
        cp {"$TMP","$TESTS"}/test_s.out
    else
        diff -u {"$TESTS","$TMP"}/test_s.out
    fi
}

function test_hidden() {
    bfs_diff weirdnames -hidden
}

function test_nohidden() {
    bfs_diff weirdnames -nohidden
}

function test_depth_n() {
    bfs_diff basic -depth 2
}

function test_depth_n_plus() {
    bfs_diff basic -depth +2
}

function test_depth_n_minus() {
    bfs_diff basic -depth -2
}

function test_depth_depth_n() {
    bfs_diff basic -depth -depth 2
}

function test_depth_depth_n_plus() {
    bfs_diff basic -depth -depth +2
}

function test_depth_depth_n_minus() {
    bfs_diff basic -depth -depth -2
}

function test_depth_overflow() {
    bfs_diff basic -depth -4294967296
}

function test_gid_name() {
    bfs_diff basic -gid "$(id -gn)"
}

function test_uid_name() {
    bfs_diff basic -uid "$(id -un)"
}

function test_mnewer() {
    bfs_diff times -mnewer times/a
}

function test_H_mnewer() {
    bfs_diff -H times -mnewer times/l
}

function test_msince() {
    bfs_diff times -msince 1991-12-14T00:01
}

function test_size_T() {
    bfs_diff basic -type f -size 1T
}

function test_quit() {
    bfs_diff basic/g -print -name g -quit
}

function test_quit_child() {
    bfs_diff basic/g -print -name h -quit
}

function test_quit_depth() {
    bfs_diff basic/g -depth -print -name g -quit
}

function test_quit_depth_child() {
    bfs_diff basic/g -depth -print -name h -quit
}

function test_quit_after_print() {
    bfs_diff basic basic -print -quit
}

function test_quit_before_print() {
    bfs_diff basic basic -quit -print
}

function test_quit_implicit_print() {
    bfs_diff basic -name basic -o -quit
}

function test_inum() {
    bfs_diff basic -inum "$(inum basic/k/foo/bar)"
}

function test_nogroup() {
    bfs_diff basic -nogroup
}

function test_nogroup_ulimit() {
    closefrom 4
    ulimit -n 16
    bfs_diff deep -nogroup
}

function test_nouser() {
    bfs_diff basic -nouser
}

function test_nouser_ulimit() {
    closefrom 4
    ulimit -n 16
    bfs_diff deep -nouser
}

function test_printf() {
    bfs_diff basic -printf '%%p(%p) %%d(%d) %%f(%f) %%h(%h) %%H(%H) %%P(%P) %%m(%m) %%M(%M) %%y(%y)\n'
}

function test_printf_empty() {
    bfs_diff basic -printf ''
}

function test_printf_slash() {
    bfs_diff / -maxdepth 0 -printf '(%h)/(%f)\n'
}

function test_printf_slashes() {
    bfs_diff /// -maxdepth 0 -printf '(%h)/(%f)\n'
}

function test_printf_trailing_slash() {
    bfs_diff basic/ -printf '(%h)/(%f)\n'
}

function test_printf_trailing_slashes() {
    bfs_diff basic/// -printf '(%h)/(%f)\n'
}

function test_printf_flags() {
    bfs_diff basic -printf '|%- 10.10p| %+03d %#4m\n'
}

function test_printf_types() {
    bfs_diff loops -printf '(%p) (%l) %y %Y\n'
}

function test_printf_escapes() {
    bfs_diff basic -maxdepth 0 -printf '\18\118\1118\11118\n\cfoo'
}

function test_printf_times() {
    bfs_diff times -type f -printf '%p | %a %AY-%Am-%Ad %AH:%AI:%AS %T@ | %t %TY-%Tm-%Td %TH:%TI:%TS %T@\n'
}

function test_printf_leak() {
    # Memory leak regression test
    bfs_diff basic -maxdepth 0 -printf '%p'
}

function test_printf_nul() {
    # NUL byte regression test
    local EXPECTED="$TESTS/${FUNCNAME[0]}.out"
    if [ "$UPDATE" ]; then
        local ACTUAL="$EXPECTED"
    else
        local ACTUAL="$TMP/${FUNCNAME[0]}.out"
    fi

    invoke_bfs basic -maxdepth 0 -printf '%h\0%f\n' >"$ACTUAL"

    if [ ! "$UPDATE" ]; then
        diff -u "$EXPECTED" "$ACTUAL"
    fi
}

function test_printf_w() {
    # Birth times may not be supported, so just check that %w/%W/%B can be parsed
    bfs_diff times -false -printf '%w %WY %BY\n'
}

function test_printf_Y_error() {
    rm -rf scratch/*
    mkdir scratch/foo
    chmod -x scratch/foo
    ln -s foo/bar scratch/bar

    bfs_diff scratch -printf '(%p) (%l) %y %Y\n' 2>/dev/null
    local ret=$?

    chmod +x scratch/foo
    rm -rf scratch/*

    [ $ret -eq $EX_BFS ]
}

function test_printf_H() {
    bfs_diff basic links -printf '%%p(%p) %%d(%d) %%f(%f) %%h(%h) %%H(%H) %%P(%P) %%y(%y)\n'
}

function test_printf_u_g_ulimit() {
    closefrom 4
    ulimit -n 16
    [ "$(invoke_bfs deep -printf '%u %g\n' | uniq)" = "$(id -un) $(id -gn)" ]
}

function test_fstype() {
    fstype="$(invoke_bfs basic -maxdepth 0 -printf '%F\n')"
    bfs_diff basic -fstype "$fstype"
}

function test_path_flag_expr() {
    bfs_diff links/skip -H -type l
}

function test_path_expr_flag() {
    bfs_diff links/skip -type l -H
}

function test_flag_expr_path() {
    bfs_diff -H -type l links/skip
}

function test_expr_flag_path() {
    bfs_diff -type l -H links/skip
}

function test_expr_path_flag() {
    bfs_diff -type l links/skip -H
}

function test_parens() {
    bfs_diff basic \( -name '*f*' \)
}

function test_bang() {
    bfs_diff basic \! -name foo
}

function test_not() {
    bfs_diff basic -not -name foo
}

function test_implicit_and() {
    bfs_diff basic -name foo -type d
}

function test_a() {
    bfs_diff basic -name foo -a -type d
}

function test_and() {
    bfs_diff basic -name foo -and -type d
}

function test_o() {
    bfs_diff basic -name foo -o -type d
}

function test_or() {
    bfs_diff basic -name foo -or -type d
}

function test_comma() {
    bfs_diff basic -name '*f*' -print , -print
}

function test_precedence() {
    bfs_diff basic \( -name foo -type d -o -name bar -a -type f \) -print , \! -empty -type f -print
}

function test_color() {
    LS_COLORS= bfs_diff rainbow -color
}

function test_color_L() {
    LS_COLORS= bfs_diff -L rainbow -color
}

function test_color_rs_lc_rc_ec() {
    LS_COLORS="rs=RS:lc=LC:rc=RC:ec=EC:" bfs_diff rainbow -color
}

function test_color_escapes() {
    LS_COLORS="lc=\e[:rc=\155\::ec=^[\x5B\x6d:" bfs_diff rainbow -color
}

function test_color_nul() {
    local EXPECTED="$TESTS/${FUNCNAME[0]}.out"
    if [ "$UPDATE" ]; then
        local ACTUAL="$EXPECTED"
    else
        local ACTUAL="$TMP/${FUNCNAME[0]}.out"
    fi

    LS_COLORS="ec=\33[m\0:" invoke_bfs rainbow -color -maxdepth 0 >"$ACTUAL"

    if [ ! "$UPDATE" ]; then
        diff -u "$EXPECTED" "$ACTUAL"
    fi
}

function test_color_ln_target() {
    LS_COLORS="ln=target:or=01;31:mi=01;33:" bfs_diff rainbow -color
}

function test_color_L_ln_target() {
    LS_COLORS="ln=target:or=01;31:mi=01;33:" bfs_diff -L rainbow -color
}

function test_color_mh() {
    LS_COLORS="mh=01:" bfs_diff rainbow -color
}

function test_color_mh0() {
    LS_COLORS="mh=00:" bfs_diff rainbow -color
}

function test_color_or() {
    LS_COLORS="or=01:" bfs_diff rainbow -color
}

function test_color_mi() {
    LS_COLORS="mi=01:" bfs_diff rainbow -color
}

function test_color_or_mi() {
    LS_COLORS="or=01;31:mi=01;33:" bfs_diff rainbow -color
}

function test_color_or_mi0() {
    LS_COLORS="or=01;31:mi=00:" bfs_diff rainbow -color
}

function test_color_or0_mi() {
    LS_COLORS="or=00:mi=01;33:" bfs_diff rainbow -color
}

function test_color_or0_mi0() {
    LS_COLORS="or=00:mi=00:" bfs_diff rainbow -color
}

function test_color_su_sg0() {
    LS_COLORS="su=37;41:sg=00:" bfs_diff rainbow -color
}

function test_color_su0_sg() {
    LS_COLORS="su=00:sg=30;43:" bfs_diff rainbow -color
}

function test_color_su0_sg0() {
    LS_COLORS="su=00:sg=00:" bfs_diff rainbow -color
}

function test_color_st_tw_ow0() {
    LS_COLORS="st=37;44:tw=40;32:ow=00:" bfs_diff rainbow -color
}

function test_color_st_tw0_ow() {
    LS_COLORS="st=37;44:tw=00:ow=34;42:" bfs_diff rainbow -color
}

function test_color_st_tw0_ow0() {
    LS_COLORS="st=37;44:tw=00:ow=00:" bfs_diff rainbow -color
}

function test_color_st0_tw_ow() {
    LS_COLORS="st=00:tw=40;32:ow=34;42:" bfs_diff rainbow -color
}

function test_color_st0_tw_ow0() {
    LS_COLORS="st=00:tw=40;32:ow=00:" bfs_diff rainbow -color
}

function test_color_st0_tw0_ow() {
    LS_COLORS="st=00:tw=00:ow=34;42:" bfs_diff rainbow -color
}

function test_color_st0_tw0_ow0() {
    LS_COLORS="st=00:tw=00:ow=00:" bfs_diff rainbow -color
}

function test_color_ext() {
    LS_COLORS="*.txt=01:" bfs_diff rainbow -color
}

function test_color_ext0() {
    LS_COLORS="*.txt=00:" bfs_diff rainbow -color
}

function test_color_ext_override() {
    LS_COLORS="*.tar.gz=01;31:*.tar=01;32:*.gz=01;33:" bfs_diff rainbow -color
}

function test_color_ext_underride() {
    LS_COLORS="*.gz=01;33:*.tar=01;32:*.tar.gz=01;31:" bfs_diff rainbow -color
}

function test_color_missing_colon() {
    LS_COLORS="*.txt=01" bfs_diff rainbow -color
}

function test_color_no_stat() {
    LS_COLORS="mh=0:ex=0:sg=0:su=0:st=0:ow=0:tw=0:*.txt=01:" bfs_diff rainbow -color
}

function test_color_L_no_stat() {
    LS_COLORS="mh=0:ex=0:sg=0:su=0:st=0:ow=0:tw=0:*.txt=01:" bfs_diff -L rainbow -color
}

function test_color_star() {
    # Regression test: don't segfault on LS_COLORS="*"
    LS_COLORS="*" bfs_diff rainbow -color
}

function test_color_ls() {
    rm -rf scratch/*
    touchp scratch/foo/bar/baz
    ln -s foo/bar/baz scratch/link
    ln -s foo/bar/nowhere scratch/broken
    ln -s foo/bar/nowhere/nothing scratch/nested
    ln -s foo/bar/baz/qux scratch/notdir
    ln -s scratch/foo/bar scratch/relative
    mkdir scratch/__bfs__
    ln -s /__bfs__/nowhere scratch/absolute

    local EXPECTED="$TESTS/${FUNCNAME[0]}.out"
    if [ "$UPDATE" ]; then
        local ACTUAL="$EXPECTED"
    else
        local ACTUAL="$TMP/${FUNCNAME[0]}.out"
    fi

    LS_COLORS="or=01;31:" invoke_bfs scratch/{,link,broken,nested,notdir,relative,absolute} -color -type l -ls \
        | sed 's/.* -> //' \
        | sort -o "$ACTUAL"

    if [ ! "$UPDATE" ]; then
        diff -u "$EXPECTED" "$ACTUAL"
    fi
}

function test_deep() {
    closefrom 4

    ulimit -n 16
    bfs_diff deep -type f -exec bash -c 'echo "${1:0:6}/.../${1##*/} (${#1})"' bash '{}' \;
}

function test_deep_strict() {
    closefrom 4

    # Not even enough fds to keep the root open
    ulimit -n 7
    bfs_diff deep -type f -exec bash -c 'echo "${1:0:6}/.../${1##*/} (${#1})"' bash '{}' \;
}

function test_exit() {
    invoke_bfs basic -name foo -exit 42
    if [ $? -ne 42 ]; then
        return 1
    fi

    invoke_bfs basic -name qux -exit 42
    if [ $? -ne 0 ]; then
        return 1
    fi

    bfs_diff basic/g -print -name g -exit
}

function test_printx() {
    bfs_diff weirdnames -printx
}

function test_and_purity() {
    # Regression test: (-a lhs(pure) rhs(always_false)) <==> rhs is only valid if rhs is pure
    bfs_diff basic -name nonexistent \( -print , -false \)
}

function test_or_purity() {
    # Regression test: (-o lhs(pure) rhs(always_true)) <==> rhs is only valid if rhs is pure
    bfs_diff basic -name '*' -o -print
}

function test_double_negation() {
    bfs_diff basic \! \! -name 'foo'
}

function test_not_reachability() {
    bfs_diff basic -print \! -quit -print
}

function test_comma_reachability() {
    bfs_diff basic -print -quit , -print
}

function test_de_morgan_not() {
    bfs_diff basic \! \( -name 'foo' -o \! -type f \)
}

function test_de_morgan_and() {
    bfs_diff basic \( \! -name 'foo' -a \! -type f \)
}

function test_de_morgan_or() {
    bfs_diff basic \( \! -name 'foo' -o \! -type f \)
}

function test_and_false_or_true() {
    # Test (-a lhs(always_true) -false) <==> (! lhs),
    # (-o lhs(always_false) -true) <==> (! lhs)
    bfs_diff basic -prune -false -o -true
}

function test_comma_redundant_true() {
    # Test (, lhs(always_true) -true) <==> lhs
    bfs_diff basic -prune , -true
}

function test_comma_redundant_false() {
    # Test (, lhs(always_false) -false) <==> lhs
    bfs_diff basic -print -not -prune , -false
}

function test_data_flow_depth() {
    bfs_diff basic -depth +1 -depth -4
}

function test_data_flow_type() {
    bfs_diff basic \! \( -type f -o \! -type f \)
}

function test_data_flow_and_swap() {
    bfs_diff basic \! -type f -a -type d
}

function test_data_flow_or_swap() {
    bfs_diff basic \! \( -type f -o \! -type d \)
}

function test_print_error() {
    if [ -e /dev/full ]; then
        ! invoke_bfs basic -maxdepth 0 >/dev/full 2>/dev/null
    fi
}

function test_fprint_error() {
    if [ -e /dev/full ]; then
        ! invoke_bfs basic -maxdepth 0 -fprint /dev/full 2>/dev/null
    fi
}

function test_closed_stdin() {
    bfs_diff basic <&-
}

function test_ok_closed_stdin() {
    bfs_diff basic -ok echo \; <&- 2>/dev/null
}

function test_okdir_closed_stdin() {
    bfs_diff basic -okdir echo {} \; <&- 2>/dev/null
}

function test_closed_stdout() {
    ! invoke_bfs basic >&- 2>/dev/null
}

function test_closed_stderr() {
    ! invoke_bfs basic >&- 2>&-
}

function test_unique() {
    bfs_diff links/{file,symlink,hardlink} -unique
}

function test_unique_depth() {
    bfs_diff basic -unique -depth
}

function test_L_unique() {
    bfs_diff -L links/{file,symlink,hardlink} -unique
}

function test_L_unique_loops() {
    bfs_diff -L loops/deeply/nested -unique
}

function test_L_unique_depth() {
    bfs_diff -L loops/deeply/nested -unique -depth
}

function test_mount() {
    rm -rf scratch/*
    mkdir scratch/{foo,mnt}
    sudo mount -t tmpfs tmpfs scratch/mnt
    touch scratch/foo/bar scratch/mnt/baz

    bfs_diff scratch -mount
    local ret=$?

    sudo umount scratch/mnt
    return $ret
}

function test_L_mount() {
    rm -rf scratch/*
    mkdir scratch/{foo,mnt}
    sudo mount -t tmpfs tmpfs scratch/mnt
    ln -s ../mnt scratch/foo/bar
    touch scratch/mnt/baz
    ln -s ../mnt/baz scratch/foo/qux

    bfs_diff -L scratch -mount
    local ret=$?

    sudo umount scratch/mnt
    return $ret
}

function test_xdev() {
    rm -rf scratch/*
    mkdir scratch/{foo,mnt}
    sudo mount -t tmpfs tmpfs scratch/mnt
    touch scratch/foo/bar scratch/mnt/baz

    bfs_diff scratch -xdev
    local ret=$?

    sudo umount scratch/mnt
    return $ret
}

function test_L_xdev() {
    rm -rf scratch/*
    mkdir scratch/{foo,mnt}
    sudo mount -t tmpfs tmpfs scratch/mnt
    ln -s ../mnt scratch/foo/bar
    touch scratch/mnt/baz
    ln -s ../mnt/baz scratch/foo/qux

    bfs_diff -L scratch -xdev
    local ret=$?

    sudo umount scratch/mnt
    return $ret
}

function test_inum_mount() {
    rm -rf scratch/*
    mkdir scratch/{foo,mnt}
    sudo mount -t tmpfs tmpfs scratch/mnt

    bfs_diff scratch -inum "$(inum scratch/mnt)"
    local ret=$?

    sudo umount scratch/mnt
    return $ret
}

function test_inum_bind_mount() {
    rm -rf scratch/*
    touch scratch/{foo,bar}
    sudo mount --bind scratch/{foo,bar}

    bfs_diff scratch -inum "$(inum scratch/bar)"
    local ret=$?

    sudo umount scratch/bar
    return $ret
}

function test_type_bind_mount() {
    rm -rf scratch/*
    touch scratch/{file,null}
    sudo mount --bind /dev/null scratch/null

    bfs_diff scratch -type c
    local ret=$?

    sudo umount scratch/null
    return $ret
}

function test_xtype_bind_mount() {
    rm -rf scratch/*
    touch scratch/{file,null}
    sudo mount --bind /dev/null scratch/null
    ln -s /dev/null scratch/link

    bfs_diff -L scratch -type c
    local ret=$?

    sudo umount scratch/null
    return $ret
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

function test_acl() {
    rm -rf scratch/*

    invoke_bfs scratch -quit -acl 2>/dev/null || return 0

    touch scratch/{normal,acl}
    set_acl scratch/acl || return 0
    ln -s acl scratch/link

    bfs_diff scratch -acl
}

function test_L_acl() {
    rm -rf scratch/*

    invoke_bfs scratch -quit -acl 2>/dev/null || return 0

    touch scratch/{normal,acl}
    set_acl scratch/acl || return 0
    ln -s acl scratch/link

    bfs_diff -L scratch -acl
}

function test_capable() {
    rm -rf scratch/*

    if ! invoke_bfs scratch -quit -capable 2>/dev/null; then
        return 0
    fi

    touch scratch/{normal,capable}
    sudo setcap all+ep scratch/capable
    ln -s capable scratch/link

    bfs_diff scratch -capable
}

function test_L_capable() {
    rm -rf scratch/*

    if ! invoke_bfs scratch -quit -capable 2>/dev/null; then
        return 0
    fi

    touch scratch/{normal,capable}
    sudo setcap all+ep scratch/capable
    ln -s capable scratch/link

    bfs_diff -L scratch -capable
}

function set_xattr() {
    case "$UNAME" in
        Darwin)
            xattr -w bfs_test true "$1"
            xattr -s -w bfs_test true "$2"
            ;;
        FreeBSD)
            setextattr user bfs_test true "$1"
            setextattr -h user bfs_test true "$2"
            ;;
        *)
            # Linux tmpfs doesn't support the user.* namespace, so we use the security.*
            # namespace, which is writable by root and readable by others
            sudo setfattr -n security.bfs_test "$1"
            sudo setfattr -h -n security.bfs_test "$2"
            ;;
    esac
}

function test_xattr() {
    rm -rf scratch/*

    if ! invoke_bfs scratch -quit -xattr 2>/dev/null; then
        return 0
    fi

    touch scratch/{normal,xattr}
    ln -s xattr scratch/link
    ln -s normal scratch/xattr_link
    set_xattr scratch/xattr scratch/xattr_link

    bfs_diff scratch -xattr
}

function test_L_xattr() {
    rm -rf scratch/*

    if ! invoke_bfs scratch -quit -xattr 2>/dev/null; then
        return 0
    fi

    touch scratch/{normal,xattr}
    ln -s xattr scratch/link
    ln -s normal scratch/xattr_link
    set_xattr scratch/xattr scratch/xattr_link

    bfs_diff -L scratch -xattr
}

function test_help() {
    invoke_bfs -help | grep -E '\{...?\}' && return 1
    invoke_bfs -D help | grep -E '\{...?\}' && return 1
    invoke_bfs -S help | grep -E '\{...?\}' && return 1
    invoke_bfs -regextype help | grep -E '\{...?\}' && return 1

    return 0
}

function test_S() {
    invoke_bfs -S "$1" -s basic >"$TMP/test_S_$1.out"

    if [ "$UPDATE" ]; then
        cp {"$TMP","$TESTS"}/"test_S_$1.out"
    else
        diff -u {"$TESTS","$TMP"}/"test_S_$1.out"
    fi
}

function test_S_bfs() {
    test_S bfs
}

function test_S_dfs() {
    test_S dfs
}

function test_S_ids() {
    test_S ids
}


BOL=
EOL='\n'

function update_eol() {
    # Put the cursor at the last column, then write a space so the next
    # character will wrap
    EOL="\\033[${COLUMNS}G "
}

if [ -t 1 -a ! "$VERBOSE" ]; then
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

for test in "${enabled_tests[@]}"; do
    printf "${BOL}${YLW}%s${RST}${EOL}" "$test"

    ("$test")
    status=$?

    if [ $status -eq 0 ]; then
        ((++passed))
    else
        ((++failed))
        printf "${BOL}${RED}%s failed!${RST}\n" "$test"
    fi
done

if [ $passed -gt 0 ]; then
    printf "${BOL}${GRN}tests passed: %d${RST}\n" "$passed"
fi
if [ $failed -gt 0 ]; then
    printf "${BOL}${RED}tests failed: %s${RST}\n" "$failed"
    exit 1
fi
