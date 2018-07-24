#!/bin/bash

############################################################################
# bfs                                                                      #
# Copyright (C) 2015-2017 Tavian Barnes <tavianator@tavianator.com>        #
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

set -o physical
umask 022

export LC_ALL=C
export TZ=UTC

# The temporary directory that will hold our test data
TMP="$(mktemp -d "${TMPDIR:-/tmp}"/bfs.XXXXXXXXXX)"
chown "$(id -u):$(id -g)" "$TMP"

# Clean up temporary directories on exit
CLEAN=yes
function cleanup() {
    if [ ! "$CLEAN" ]; then
        return
    fi

    # Don't force rm to deal with long paths
    for dir in "$TMP"/deep/*/*; do
        if [ -d "$dir" ]; then
            (cd "$dir" && rm -rf *)
        fi
    done

    rm -rf "$TMP"
}
trap cleanup EXIT

# Install a file, creating any parent directories
function installp() {
    local target="${@: -1}"
    mkdir -p "${target%/*}"
    install "$@"
}

# Like a mythical touch -p
function touchp() {
    installp -m644 /dev/null "$1"
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
    ln -s loop "$1/loop"
    mkdir -p "$1/deeply/nested/dir"
    ln -s ../../deeply "$1/deeply/nested/loop"
    ln -s deeply/nested/loop/nested "$1/skip"
}
make_links "$TMP/links"

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

            # 17 * 256 > 16 * 256 == 4096 == PATH_MAX
            for j in {1..17}; do
                mkdir "$name"
                cd "$name" 2>/dev/null
            done
        )
    done
}
make_deep "$TMP/deep"

# Creates a scratch directory that tests can modify
function make_scratch() {
    mkdir -p "$1"
}
make_scratch "$TMP/scratch"

function _realpath() {
    (
        cd "${1%/*}"
        echo "$PWD/${1##*/}"
    )
}

BFS="$(_realpath ./bfs)"
TESTS="$(_realpath ./tests)"

posix_tests=(
    test_basic
    test_type_d
    test_type_f
    test_mindepth
    test_maxdepth
    test_depth
    test_depth_slash
    test_depth_mindepth_1
    test_depth_mindepth_2
    test_depth_maxdepth_1
    test_depth_maxdepth_2
    test_depth_error
    test_name
    test_name_root
    test_name_root_depth
    test_name_trailing_slash
    test_path
    test_newer
    test_newer_link
    test_links
    test_links_plus
    test_links_minus
    test_H
    test_H_slash
    test_H_broken
    test_H_notdir
    test_H_newer
    test_L
    test_L_broken
    test_L_notdir
    test_L_depth
    test_user_name
    test_user_id
    test_group_name
    test_group_id
    test_size
    test_size_plus
    test_size_bytes
    test_exec
    test_exec_plus
    test_exec_plus_status
    test_exec_plus_semicolon
    test_flag_comma
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
    test_ok_stdin
    test_ok_plus_semicolon
    test_parens
    test_bang
    test_implicit_and
    test_a
    test_o
    test_deep
    test_double_negation
    test_de_morgan_not
    test_de_morgan_and
    test_de_morgan_or
    test_data_flow_type
)

bsd_tests=(
    test_P
    test_P_slash
    test_X
    test_follow
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
    test_name_slash
    test_name_slashes
    test_iname
    test_ipath
    test_lname
    test_ilname
    test_L_lname
    test_L_ilname
    test_newerma
    test_size_big
    test_exec_substring
    test_execdir_pwd
    test_execdir_slash
    test_execdir_slash_pwd
    test_execdir_slashes
    test_double_dash
    test_flag_double_dash
    test_ok_stdin
    test_okdir_stdin
    test_delete
    test_L_delete
    test_rm
    test_regex
    test_iregex
    test_regex_parens
    test_E
    test_d_path
    test_f
    test_depth_n
    test_depth_n_plus
    test_depth_n_minus
    test_depth_depth_n
    test_depth_depth_n_plus
    test_depth_depth_n_minus
    test_depth_overflow
    test_gid_name
    test_uid_name
    test_mnewer
    test_H_mnewer
    test_perm_000_plus
    test_perm_222_plus
    test_perm_644_plus
    test_size_T
    test_quit
    test_quit_child
    test_quit_depth
    test_quit_depth_child
    test_quit_after_print
    test_quit_before_print
    test_quit_implicit_print
    test_inum
    test_nogroup
    test_nouser
    test_exit
    test_printx
    test_data_flow_depth
)

gnu_tests=(
    test_true
    test_false
    test_executable
    test_readable
    test_writable
    test_empty
    test_gid
    test_gid_plus
    test_gid_minus
    test_uid
    test_uid_plus
    test_uid_minus
    test_anewer
    test_P
    test_P_slash
    test_follow
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
    test_xtype_l
    test_xtype_f
    test_L_xtype_l
    test_L_xtype_f
    test_name_slash
    test_name_slashes
    test_iname
    test_ipath
    test_lname
    test_ilname
    test_L_lname
    test_L_ilname
    test_daystart
    test_daystart_twice
    test_newerma
    test_size_big
    test_exec_substring
    test_execdir
    test_execdir_substring
    test_execdir_plus_semicolon
    test_execdir_pwd
    test_execdir_slash
    test_execdir_slash_pwd
    test_execdir_slashes
    test_okdir_plus_semicolon
    test_weird_names
    test_flag_weird_names
    test_follow_comma
    test_fprint
    test_fprint_duplicate
    test_double_dash
    test_flag_double_dash
    test_ignore_readdir_race
    test_ignore_readdir_race_root
    test_ignore_readdir_race_notdir
    test_perm_000_slash
    test_perm_222_slash
    test_perm_644_slash
    test_perm_symbolic_slash
    test_perm_leading_plus_symbolic_slash
    test_delete
    test_L_delete
    test_regex
    test_iregex
    test_regex_parens
    test_regextype_posix_basic
    test_regextype_posix_extended
    test_path_d
    test_quit
    test_quit_child
    test_quit_depth
    test_quit_depth_child
    test_inum
    test_nogroup
    test_nouser
    test_printf
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
    test_quit_after_print
    test_quit_before_print
    test_fstype
    test_not
    test_and
    test_or
    test_comma
    test_precedence
    test_and_purity
    test_or_purity
    test_not_reachability
    test_comma_reachability
    test_print_error
    test_fprint_error
)

bfs_tests=(
    test_type_multi
    test_xtype_multi
    test_xtype_reorder
    test_perm_symbolic_trailing_comma
    test_perm_symbolic_double_comma
    test_perm_symbolic_missing_action
    test_perm_leading_plus_symbolic
    test_execdir_plus
    test_hidden
    test_nohidden
    test_printf_w
    test_path_flag_expr
    test_path_expr_flag
    test_flag_expr_path
    test_expr_flag_path
    test_expr_path_flag
    test_colors
    test_deep_strict
    test_deep_stricter
)

BSD=yes
GNU=yes
ALL=yes

function enable_tests() {
    for test; do
        eval run_$test=yes
    done
}

for arg; do
    case "$arg" in
        --bfs=*)
            BFS="${arg#*=}"
            ;;
        --posix)
            BSD=
            GNU=
            ALL=
            ;;
        --bsd)
            BSD=yes
            GNU=
            ALL=
            ;;
        --gnu)
            BSD=
            GNU=yes
            ALL=
            ;;
        --all)
            BSD=yes
            GNU=yes
            ALL=yes
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
        test_*)
            EXPLICIT=yes
            enable_tests "$arg"
            ;;
        *)
            echo "Unrecognized option '$arg'." >&2
            exit 1
            ;;
    esac
done

if [ ! "$CLEAN" ]; then
    echo "Test files saved to $TMP"
fi

if [ ! "$EXPLICIT" ]; then
    enable_tests "${posix_tests[@]}"
    [ "$BSD" ] && enable_tests "${bsd_tests[@]}"
    [ "$GNU" ] && enable_tests "${gnu_tests[@]}"
    [ "$ALL" ] && enable_tests "${bfs_tests[@]}"
fi

function bfs_sort() {
    awk -F/ '{ print NF - 1 " " $0 }' | sort -n | cut -d' ' -f2-
}


if [ "$VERBOSE" ]; then
    # dup stdout for verbose logging even when redirected
    exec 3>&1
fi

function bfs_verbose() {
    if [ "$VERBOSE" ]; then
        if [ -t 3 ]; then
            printf '\033[1;32m%q\033[0m ' $BFS >&3

            local expr_started=
            for arg; do
                if [[ $arg == -[A-Z]* ]]; then
                    printf '\033[1;36m%q\033[0m ' "$arg" >&3
                elif [[ $arg == [\(!] || $arg == -[ao] || $arg == -and || $arg == -or || $arg == -not ]]; then
                    expr_started=yes
                    printf '\033[1;31m%q\033[0m ' "$arg" >&3
                elif [[ $expr_started && $arg == [\),] ]]; then
                    printf '\033[1;31m%q\033[0m ' "$arg" >&3
                elif [[ $arg == -?* ]]; then
                    expr_started=yes
                    printf '\033[1;34m%q\033[0m ' "$arg" >&3
                elif [ "$expr_started" ]; then
                    printf '\033[1m%q\033[0m ' "$arg" >&3
                else
                    printf '\033[1;35m%q\033[0m ' "$arg" >&3
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

    if [ ! "$UPDATE" ]; then
        diff -u "$EXPECTED" "$ACTUAL"
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

cd "$TMP"

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

    return $ret
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
    bfs_diff / -maxdepth 0 -name / 2>/dev/null
}

function test_name_slashes() {
    bfs_diff /// -maxdepth 0 -name / 2>/dev/null
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

function test_gid() {
    bfs_diff basic -gid "$(id -g)"
}

function test_gid_plus() {
    bfs_diff basic -gid +0
}

function test_gid_minus() {
    bfs_diff basic -gid "-$(($(id -g) + 1))"
}

function test_uid() {
    bfs_diff basic -uid "$(id -u)"
}

function test_uid_plus() {
    bfs_diff basic -uid +0
}

function test_uid_minus() {
    bfs_diff basic -uid "-$(($(id -u) + 1))"
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

function test_L() {
    bfs_diff -L links 2>/dev/null
}

function test_L_broken() {
    bfs_diff -H links/broken
}

function test_L_notdir() {
    bfs_diff -H links/notdir
}

function test_X() {
    bfs_diff -X weirdnames 2>/dev/null
}

function test_follow() {
    bfs_diff links -follow 2>/dev/null
}

function test_L_depth() {
    bfs_diff -L links -depth 2>/dev/null
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
    bfs_diff -L links -samefile links/symlink 2>/dev/null
}

function test_samefile_broken() {
    bfs_diff links -samefile links/broken
}

function test_H_samefile_broken() {
    bfs_diff -H links -samefile links/broken
}

function test_L_samefile_broken() {
    bfs_diff -L links -samefile links/broken 2>/dev/null
}

function test_samefile_notdir() {
    bfs_diff links -samefile links/notdir
}

function test_H_samefile_notdir() {
    bfs_diff -H links -samefile links/notdir
}

function test_L_samefile_notdir() {
    bfs_diff -L links -samefile links/notdir 2>/dev/null
}

function test_xtype_l() {
    bfs_diff links -xtype l 2>/dev/null
}

function test_xtype_f() {
    bfs_diff links -xtype f 2>/dev/null
}

function test_L_xtype_l() {
    bfs_diff -L links -xtype l 2>/dev/null
}

function test_L_xtype_f() {
    bfs_diff -L links -xtype f 2>/dev/null
}

function test_xtype_multi() {
    bfs_diff links -xtype f,d,c 2>/dev/null
}

function test_xtype_reorder() {
    # Make sure -xtype is not reordered in front of anything -- if -xtype runs
    # before -links 100, it will report an ELOOP error
    bfs_diff links -links 100 -xtype l
    invoke_bfs links -links 100 -xtype l
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
    bfs_diff -L links -lname '[aq]' 2>/dev/null
}

function test_L_ilname() {
    bfs_diff -L links -ilname '[AQ]' 2>/dev/null
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
    bfs_diff basic -exec echo '{}' ';'
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
    bfs_diff basic -execdir "$TESTS/sort-args.sh" '{}' +
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

function test_weird_names() {
    cd weirdnames
    bfs_diff '-' '(-' '!-' ',' ')' './(' './!' \( \! -print , -print \)
}

function test_flag_weird_names() {
    cd weirdnames
    bfs_diff -L '-' '(-' '!-' ',' ')' './(' './!' \( \! -print , -print \)
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

function test_ok_stdin() {
    # -ok should *not* close stdin
    # See https://savannah.gnu.org/bugs/?24561
    yes | bfs_diff basic -ok bash -c "printf '%s? ' {} && head -n1" \; 2>/dev/null
}

function test_okdir_stdin() {
    # -okdir should *not* close stdin
    yes | bfs_diff basic -okdir bash -c "printf '%s? ' {} && head -n1" \; 2>/dev/null
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
    (cd scratch && invoke_bfs -delete)

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

    (cd scratch && invoke_bfs -rm)

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
    local inode="$(ls -id basic/k/foo/bar | awk '{ print $1 }')"
    bfs_diff basic -inum "$inode"
}

function test_nogroup() {
    bfs_diff basic -nogroup
}

function test_nouser() {
    bfs_diff basic -nouser
}

function test_printf() {
    bfs_diff basic -printf '%%p(%p) %%d(%d) %%f(%f) %%h(%h) %%H(%H) %%P(%P) %%m(%m) %%M(%M) %%y(%y)\n'
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
    bfs_diff links -printf '(%p) (%l) %y %Y\n'
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

    return $ret
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

function test_colors() {
    LS_COLORS= bfs_diff links -color
}

function test_deep() {
    closefrom 4

    ulimit -n 16
    bfs_diff deep -mindepth 18 -exec /bin/bash -c 'echo "${1:0:6}/.../${1##*/} (${#1})"' /bin/bash '{}' \;
}

function test_deep_strict() {
    closefrom 4

    # Not even enough fds to keep the root open
    ulimit -n 7
    bfs_diff deep -mindepth 18 -exec /bin/bash -c 'echo "${1:0:6}/.../${1##*/} (${#1})"' /bin/bash '{}' \;
}

function test_deep_stricter() {
    closefrom 4

    # So few fds that pipe() fails in the -exec implementation
    ulimit -n 6
    bfs_diff deep -mindepth 18 -exec /bin/bash -c 'echo "${1:0:6}/.../${1##*/} (${#1})"' /bin/bash '{}' \;
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

    bfs_diff basic -name bar -exit -o -print
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

function test_data_flow_depth() {
    bfs_diff basic -depth +1 -depth -4
}

function test_data_flow_type() {
    bfs_diff basic \! \( -type f -o \! -type f \)
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

if [ -t 1 -a ! "$VERBOSE" ]; then
    in_place=yes
fi

passed=0
failed=0

for test in ${!run_*}; do
    test=${test#run_}

    if [ "$in_place" ]; then
        printf '\r\033[J%s' "$test"
    else
        echo "$test"
    fi

    ("$test" "$dir")
    status=$?

    if [ $status -eq 0 ]; then
        ((++passed))
    else
        ((++failed))
        if [ "$in_place" ]; then
            printf '\r\033[J%s\n' "$test failed!"
        else
            echo "$test failed!"
        fi
    fi
done

if [ "$in_place" ]; then
    printf '\r\033[J'
fi

if [ $passed -gt 0 ]; then
    echo "tests passed: $passed"
fi
if [ $failed -gt 0 ]; then
    echo "tests failed: $failed"
    exit 1
fi
