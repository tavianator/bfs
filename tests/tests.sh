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

set -eP
umask 022

export LC_ALL=C
export TZ=UTC0

export ASAN_OPTIONS="abort_on_error=1"
export LSAN_OPTIONS="abort_on_error=1"
export MSAN_OPTIONS="abort_on_error=1"
export TSAN_OPTIONS="abort_on_error=1"
export UBSAN_OPTIONS="abort_on_error=1"

export LS_COLORS=""
unset BFS_COLORS

if [ -t 1 ]; then
    BLD=$(printf '\033[01m')
    RED=$(printf '\033[01;31m')
    GRN=$(printf '\033[01;32m')
    YLW=$(printf '\033[01;33m')
    BLU=$(printf '\033[01;34m')
    MAG=$(printf '\033[01;35m')
    CYN=$(printf '\033[01;36m')
    RST=$(printf '\033[0m')
fi

UNAME=$(uname)

if command -v capsh &>/dev/null; then
    if capsh --has-p=cap_dac_override &>/dev/null || capsh --has-p=cap_dac_read_search &>/dev/null; then
	if [ -n "$BFS_TRIED_DROP" ]; then
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
       $pad [${BLD}test_*${RST} [${BLD}test_*${RST} ...]]

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

  ${BLD}test_*${RST}
      Select individual test cases to run
EOF
}

DEFAULT=yes
POSIX=
BSD=
GNU=
ALL=
SUDO=
STOP=
CLEAN=yes
UPDATE=
VERBOSE_COMMANDS=
VERBOSE_ERRORS=
VERBOSE_SKIPPED=
VERBOSE_TESTS=
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
        test_*)
            EXPLICIT=yes
            SUDO=yes
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

    test_incomplete
    test_missing_paren
    test_extra_paren

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
    test_group_nogroup

    test_links
    test_links_plus
    test_links_minus

    test_name
    test_name_root
    test_name_root_depth
    test_name_trailing_slash
    test_name_star_star
    test_name_character_class
    test_name_bracket
    test_name_backslash
    test_name_double_backslash

    test_newer
    test_newer_link

    test_nogroup
    test_nogroup_ulimit

    test_nouser
    test_nouser_ulimit

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
    test_perm_setid
    test_perm_sticky

    test_prune
    test_prune_file
    test_prune_or_print
    test_not_prune

    test_size
    test_size_plus
    test_size_bytes

    test_type_d
    test_type_f
    test_type_l
    test_H_type_l
    test_L_type_l
    test_type_bind_mount

    test_user_name
    test_user_id
    test_user_nouser

    test_xdev
    test_L_xdev

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
    test_data_flow_group
    test_data_flow_user
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
    test_delete_many

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
    test_exit_no_implicit_print

    test_flags

    test_follow

    test_gid_name

    test_ilname
    test_L_ilname

    test_iname

    test_inum
    test_inum_mount
    test_inum_bind_mount

    test_ipath

    test_iregex

    test_lname
    test_L_lname

    test_ls
    test_L_ls

    test_maxdepth

    test_mindepth

    test_mnewer
    test_H_mnewer

    test_mount
    test_L_mount

    test_msince

    test_mtime_units

    test_name_slash
    test_name_slashes

    test_H_newer

    test_newerma
    test_newermt
    test_newermt_epoch_minus_one

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

    test_xattr
    test_L_xattr

    test_xattrname
    test_L_xattrname

    # Optimizer tests
    test_data_flow_sparse
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
    test_delete_many
    test_L_delete

    test_depth_mindepth_1
    test_depth_mindepth_2
    test_depth_maxdepth_1
    test_depth_maxdepth_2

    test_empty
    test_empty_special

    test_exec_nothing
    test_exec_substring
    test_exec_flush
    test_exec_flush_fail
    test_exec_plus_flush
    test_exec_plus_flush_fail

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

    test_files0_from_file
    test_files0_from_stdin
    test_files0_from_none
    test_files0_from_empty
    test_files0_from_nowhere
    test_files0_from_nothing
    test_files0_from_ok

    test_fls

    test_follow

    test_fprint
    test_fprint_duplicate
    test_fprint_error
    test_fprint_noerror
    test_fprint_noarg
    test_fprint_nonexistent
    test_fprint_truncate

    test_fprint0

    test_fprintf
    test_fprintf_nofile
    test_fprintf_noformat

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
    test_inum_mount
    test_inum_bind_mount
    test_inum_automount

    test_ipath

    test_iregex

    test_iwholename

    test_lname
    test_L_lname

    test_ls
    test_L_ls

    test_maxdepth

    test_mindepth

    test_mount
    test_L_mount

    test_name_slash
    test_name_slashes

    test_H_newer

    test_newerma
    test_newermt
    test_newermt_epoch_minus_one

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

    test_print0

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
    test_printf_l_nonlink

    test_quit
    test_quit_child
    test_quit_depth
    test_quit_depth_child
    test_quit_after_print
    test_quit_before_print

    test_readable

    test_regex
    test_regex_parens
    test_regex_error
    test_regex_invalid_utf8

    test_regextype_posix_basic
    test_regextype_posix_extended
    test_regextype_ed
    test_regextype_emacs
    test_regextype_grep
    test_regextype_sed

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

    test_wholename

    test_writable

    test_xtype_l
    test_xtype_f
    test_L_xtype_l
    test_L_xtype_f
    test_xtype_bind_mount

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

    test_unexpected_operator

    test_typo

    # Flags

    test_D_multi
    test_D_all

    test_O0
    test_O1
    test_O2
    test_O3
    test_Ofast

    test_S_bfs
    test_S_dfs
    test_S_ids

    # Special forms

    test_exclude_name
    test_exclude_depth
    test_exclude_mindepth
    test_exclude_print
    test_exclude_exclude

    # Primaries

    test_capable
    test_L_capable

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

    test_exec_flush_fprint
    test_exec_flush_fprint_fail

    test_execdir_plus

    test_fprint_duplicate_stdout
    test_fprint_error_stdout
    test_fprint_error_stderr

    test_help

    test_hidden
    test_hidden_root

    test_links_noarg
    test_links_empty
    test_links_negative
    test_links_invalid

    test_newerma_nonexistent
    test_newermt_invalid
    test_newermq
    test_newerqm

    test_nohidden
    test_nohidden_depth

    test_perm_symbolic_trailing_comma
    test_perm_symbolic_double_comma
    test_perm_symbolic_missing_action
    test_perm_leading_plus_symbolic

    test_printf_w
    test_printf_incomplete_escape
    test_printf_invalid_escape
    test_printf_incomplete_format
    test_printf_invalid_format
    test_printf_duplicate_flag
    test_printf_must_be_numeric
    test_printf_color

    test_type_multi

    test_unique
    test_unique_depth
    test_L_unique
    test_L_unique_loops
    test_L_unique_depth

    test_version

    test_xtype_multi

    # Optimizer tests
    test_data_flow_hidden
    test_xtype_reorder
    test_xtype_depth

    # PATH_MAX handling
    test_deep_strict

    # Error handling
    test_stderr_fails_silently
    test_stderr_fails_loudly
)

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
fi

eval "enabled_tests=($(printf '%q\n' "${enabled_tests[@]}" | sort -u))"

function _realpath() {
    (
        cd "$(dirname -- "$1")"
        echo "$PWD/$(basename -- "$1")"
    )
}

TESTS=$(_realpath "$(dirname -- "${BASH_SOURCE[0]}")")
BIN=$(_realpath "$TESTS/../bin")

# Try to resolve the path to $BFS before we cd, while also supporting
# --bfs="./bin/bfs -S ids"
read -a BFS <<<"${BFS:-$BIN/bfs}"
BFS[0]=$(_realpath "$(command -v "${BFS[0]}")")

# The temporary directory that will hold our test data
TMP=$(mktemp -d "${TMPDIR:-/tmp}"/bfs.XXXXXXXXXX)
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

# Prefer GNU touch to work around https://apple.stackexchange.com/a/425730/397839
if command -v gtouch &>/dev/null; then
    TOUCH=gtouch
else
    TOUCH=touch
fi

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
    ln -s nowhere "$1/deeply/nested/broken"
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
    $TOUCH -t 199112140000 "$1/a"
    $TOUCH -t 199112140001 "$1/b"
    $TOUCH -t 199112140002 "$1/c"
    ln -s a "$1/l"
    $TOUCH -h -t 199112140003 "$1/l"
    $TOUCH -t 199112140004 "$1"
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
    touchp "$1/[/k"
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
            for _ in {1..16}; do
                mkdir "$name"
                cd "$name" 2>/dev/null
            done

            $TOUCH "$name"
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
    "$BIN/tests/mksock" "$1/socket"
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

function bfs_diff() (
    bfs_verbose "$@"

    # Close the dup()'d stdout to make sure we have enough fd's for the process
    # substitution, even with low ulimit -n
    exec 3>&-

    local CALLER
    for CALLER in "${FUNCNAME[@]}"; do
        if [[ $CALLER == test_* ]]; then
            break
        fi
    done

    local EXPECTED="$TESTS/$CALLER.out"
    if [ "$UPDATE" ]; then
        local ACTUAL="$EXPECTED"
    else
        local ACTUAL="$TMP/$CALLER.out"
    fi

    "${BFS[@]}" "$@" | sort >"$ACTUAL"
    local STATUS="${PIPESTATUS[0]}"

    if [ ! "$UPDATE" ]; then
        $DIFF -u "$EXPECTED" "$ACTUAL" >&2 || return $EX_DIFF
    fi

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

function test_type_l() {
    bfs_diff links/skip -type l
}

function test_H_type_l() {
    bfs_diff -H links/skip -type l
}

function test_L_type_l() {
    bfs_diff -L links/skip -type l
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
    chmod a-r scratch/foo

    bfs_diff scratch -depth
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

function test_name_star_star() {
    bfs_diff basic -name '**f**'
}

function test_name_character_class() {
    bfs_diff basic -name '[e-g][!a-n][!p-z]'
}

function test_name_bracket() {
    # fnmatch() is broken on macOS
    skip_if test "$UNAME" = "Darwin"

    # An unclosed [ should be matched literally
    bfs_diff weirdnames -name '['
}

function test_name_backslash() {
    # An unescaped \ doesn't match
    bfs_diff weirdnames -name '\'
}

function test_name_double_backslash() {
    # An escaped \\ matches
    bfs_diff weirdnames -name '\\'
}

function test_path() {
    bfs_diff basic -path 'basic/*f*'
}

function test_wholename() {
    bfs_diff basic -wholename 'basic/*f*'
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
    skip_if test "$(id -g)" -eq 0
    bfs_diff basic -gid +0
}

function test_gid_plus_plus() {
    skip_if test "$(id -g)" -eq 0
    bfs_diff basic -gid ++0
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
    skip_if test "$(id -u)" -eq 0
    bfs_diff basic -uid +0
}

function test_uid_plus_plus() {
    skip_if test "$(id -u)" -eq 0
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

function test_links_noarg() {
    fail invoke_bfs links -links
}

function test_links_empty() {
    fail invoke_bfs links -links ''
}

function test_links_negative() {
    fail invoke_bfs links -links +-1
}

function test_links_invalid() {
    fail invoke_bfs links -links ASDF
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
    local errors=$(invoke_bfs -L loops 2>&1 >/dev/null)
    [ -n "$errors" ]
}

function test_L_loops_continue() {
    bfs_diff -L loops
    [ $? -eq $EX_BFS ]
}

function test_X() {
    bfs_diff -X weirdnames
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

function test_xtype_depth() {
    # Make sure -xtype is considered side-effecting for facts_when_impure
    fail invoke_bfs loops -xtype l -depth 100
}

function test_iname() {
    skip_unless invoke_bfs -quit -iname PATTERN
    bfs_diff basic -iname '*F*'
}

function test_ipath() {
    skip_unless invoke_bfs -quit -ipath PATTERN
    bfs_diff basic -ipath 'basic/*F*'
}

function test_iwholename() {
    skip_unless invoke_bfs -quit -iwholename PATTERN
    bfs_diff basic -iwholename 'basic/*F*'
}

function test_lname() {
    bfs_diff links -lname '[aq]'
}

function test_ilname() {
    skip_unless invoke_bfs -quit -ilname PATTERN
    bfs_diff links -ilname '[AQ]'
}

function test_L_lname() {
    bfs_diff -L links -lname '[aq]'
}

function test_L_ilname() {
    skip_unless invoke_bfs -quit -ilname PATTERN
    bfs_diff -L links -ilname '[AQ]'
}

function test_user_name() {
    bfs_diff basic -user "$(id -un)"
}

function test_user_id() {
    bfs_diff basic -user "$(id -u)"
}

function test_user_nouser() {
    # Regression test: this was wrongly optimized to -false
    bfs_diff basic -user "$(id -u)" \! -nouser
}

function test_group_name() {
    bfs_diff basic -group "$(id -gn)"
}

function test_group_id() {
    bfs_diff basic -group "$(id -g)"
}

function test_group_nogroup() {
    # Regression test: this was wrongly optimized to -false
    bfs_diff basic -group "$(id -g)" \! -nogroup
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

function test_newermt_invalid() {
    fail invoke_bfs times -newermt not_a_date_time
}

function test_newerma_nonexistent() {
    fail invoke_bfs times -newerma basic/nonexistent
}

function test_newermq() {
    fail invoke_bfs times -newermq times/a
}

function test_newerqm() {
    fail invoke_bfs times -newerqm times/a
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
    bfs_diff basic -exec echo {} \;
}

function test_exec_nothing() {
    # Regression test: don't segfault on missing command
    fail invoke_bfs basic -exec \;
}

function test_exec_plus() {
    bfs_diff basic -exec "$TESTS/sort-args.sh" {} +
}

function test_exec_plus_status() {
    # -exec ... {} + should always return true, but if the command fails, bfs
    # should exit with a non-zero status
    bfs_diff basic -exec false {} + -print
    (($? == EX_BFS))
}

function test_exec_plus_semicolon() {
    # POSIX says:
    #     Only a <plus-sign> that immediately follows an argument containing only the two characters "{}"
    #     shall punctuate the end of the primary expression. Other uses of the <plus-sign> shall not be
    #     treated as special.
    bfs_diff basic -exec echo foo {} bar + baz \;
}

function test_exec_substring() {
    bfs_diff basic -exec echo '-{}-' \;
}

function test_exec_flush() {
    # IO streams should be flushed before executing programs
    bfs_diff basic -print0 -exec echo found \;
}

function test_exec_flush_fail() {
    # Failure to flush streams before exec should be caught
    skip_unless test -e /dev/full
    fail invoke_bfs basic -print0 -exec true \; >/dev/full
}

function test_exec_flush_fprint() {
    # Even non-stdstreams should be flushed
    bfs_diff basic/a -fprint scratch/foo -exec cat scratch/foo \;
}

function test_exec_flush_fprint_fail() {
    skip_unless test -e /dev/full
    fail invoke_bfs basic/a -fprint /dev/full -exec true \;
}

function test_exec_plus_flush() {
    bfs_diff basic/a -print0 -exec echo found {} +
}

function test_exec_plus_flush_fail() {
    skip_unless test -e /dev/full
    fail invoke_bfs basic/a -print0 -exec echo found {} + >/dev/full
}

function test_execdir() {
    bfs_diff basic -execdir echo {} \;
}

function test_execdir_plus() {
    local tree=$(invoke_bfs -D tree 2>&1 -quit)

    if [[ "$tree" == *"-S dfs"* ]]; then
        skip
    fi

    bfs_diff basic -execdir "$TESTS/sort-args.sh" {} +
}

function test_execdir_substring() {
    bfs_diff basic -execdir echo '-{}-' \;
}

function test_execdir_plus_semicolon() {
    bfs_diff basic -execdir echo foo {} bar + baz \;
}

function test_execdir_pwd() {
    local TMP_REAL=$(cd "$TMP" && pwd)
    local OFFSET=$((${#TMP_REAL} + 2))
    bfs_diff basic -execdir bash -c "pwd | cut -b$OFFSET-" \;
}

function test_execdir_slash() {
    # Don't prepend ./ for absolute paths in -execdir
    bfs_diff / -maxdepth 0 -execdir echo {} \;
}

function test_execdir_slash_pwd() {
    bfs_diff / -maxdepth 0 -execdir pwd \;
}

function test_execdir_slashes() {
    bfs_diff /// -maxdepth 0 -execdir echo {} \;
}

function test_execdir_ulimit() {
    rm -rf scratch/*
    mkdir -p scratch/a/b/c/d/e/f/g/h/i/j/k/l/m/n/o/p/q/r/s/t/u/v/w/x/y/z
    mkdir -p scratch/a/b/c/d/e/f/g/h/i/j/k/l/m/0/1/2/3/4/5/6/7/8/9/A/B/C

    closefrom 4
    ulimit -n 13
    bfs_diff scratch -execdir echo {} \;
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
        cp {scratch,"$TESTS"}/test_fprint.out
    else
        $DIFF -u {"$TESTS",scratch}/test_fprint.out
    fi
}

function test_fprint_duplicate() {
    touchp scratch/test_fprint_duplicate.out
    ln scratch/test_fprint_duplicate.out scratch/test_fprint_duplicate.hard
    ln -s test_fprint_duplicate.out scratch/test_fprint_duplicate.soft

    invoke_bfs basic -fprint scratch/test_fprint_duplicate.out -fprint scratch/test_fprint_duplicate.hard -fprint scratch/test_fprint_duplicate.soft
    sort -o scratch/test_fprint_duplicate.out scratch/test_fprint_duplicate.out

    if [ "$UPDATE" ]; then
        cp {scratch,"$TESTS"}/test_fprint_duplicate.out
    else
        $DIFF -u {"$TESTS",scratch}/test_fprint_duplicate.out
    fi
}

function test_fprint_duplicate_stdout() {
    touchp scratch/test_fprint_duplicate_stdout.out

    invoke_bfs basic -fprint scratch/test_fprint_duplicate_stdout.out -print >scratch/test_fprint_duplicate_stdout.out
    sort -o scratch/test_fprint_duplicate_stdout.out{,}

    if [ "$UPDATE" ]; then
        cp {scratch,"$TESTS"}/test_fprint_duplicate_stdout.out
    else
        $DIFF -u {"$TESTS",scratch}/test_fprint_duplicate_stdout.out
    fi
}

function test_fprint_noarg() {
    fail invoke_bfs basic -fprint
}

function test_fprint_nonexistent() {
    fail invoke_bfs basic -fprint scratch/nonexistent/path
}

function test_fprint_truncate() {
    printf "basic\nbasic\n" >scratch/test_fprint_truncate.out

    invoke_bfs basic -maxdepth 0 -fprint scratch/test_fprint_truncate.out
    sort -o scratch/test_fprint_truncate.out scratch/test_fprint_truncate.out

    if [ "$UPDATE" ]; then
        cp {scratch,"$TESTS"}/test_fprint_truncate.out
    else
        $DIFF -u {"$TESTS",scratch}/test_fprint_truncate.out
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
    $TOUCH scratch/{foo,bar}

    # -links 1 forces a stat() call, which will fail for the second file
    invoke_bfs scratch -mindepth 1 -ignore_readdir_race -links 1 -exec "$TESTS/remove-sibling.sh" {} \;
}

function test_ignore_readdir_race_root() {
    # Make sure -ignore_readdir_race doesn't suppress ENOENT at the root
    fail invoke_bfs basic/nonexistent -ignore_readdir_race
}

function test_ignore_readdir_race_notdir() {
    # Check -ignore_readdir_race handling when a directory is replaced with a file
    rm -rf scratch/*
    touchp scratch/foo/bar

    invoke_bfs scratch -mindepth 1 -ignore_readdir_race -execdir rm -r {} \; -execdir $TOUCH {} \;
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
    fail invoke_bfs perms -perm a+r,
}

function test_perm_symbolic_double_comma() {
    fail invoke_bfs perms -perm a+r,,u+w
}

function test_perm_symbolic_missing_action() {
    fail invoke_bfs perms -perm a
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

function test_perm_setid() {
    bfs_diff rainbow -perm -u+s -o -perm -g+s
}

function test_perm_sticky() {
    bfs_diff rainbow -perm -a+t
}

function test_prune() {
    bfs_diff basic -name foo -prune
}

function test_prune_file() {
    bfs_diff basic -print -name '?' -prune
}

function test_prune_or_print() {
    bfs_diff basic -name foo -prune -o -print
}

function test_not_prune() {
    bfs_diff basic \! \( -name foo -prune \)
}

function test_ok_nothing() {
    # Regression test: don't segfault on missing command
    fail invoke_bfs basic -ok \;
}

function test_ok_stdin() {
    # -ok should *not* close stdin
    # See https://savannah.gnu.org/bugs/?24561
    yes | bfs_diff basic -ok bash -c 'printf "%s? " "$1" && head -n1' bash {} \;
}

function test_okdir_stdin() {
    # -okdir should *not* close stdin
    yes | bfs_diff basic -okdir bash -c 'printf "%s? " "$1" && head -n1' bash {} \;
}

function test_ok_plus_semicolon() {
    yes | bfs_diff basic -ok echo {} + \;
}

function test_okdir_plus_semicolon() {
    yes | bfs_diff basic -okdir echo {} + \;
}

function test_delete() {
    rm -rf scratch/*
    touchp scratch/foo/bar/baz

    # Don't try to delete '.'
    (cd scratch && invoke_bfs . -delete)

    bfs_diff scratch
}

function test_delete_many() {
    # Test for https://github.com/tavianator/bfs/issues/67

    rm -rf scratch/*
    mkdir scratch/foo
    $TOUCH scratch/foo/{1..256}

    invoke_bfs scratch/foo -delete
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

function test_regex_error() {
    fail invoke_bfs basic -regex '['
}

function test_regex_invalid_utf8() {
    rm -rf scratch/*

    # Incomplete UTF-8 sequences
    skip_unless touch scratch/$'\xC3'
    skip_unless touch scratch/$'\xE2\x84'
    skip_unless touch scratch/$'\xF0\x9F\x92'

    bfs_diff scratch -regex 'scratch/..'
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

function test_regextype_ed() {
    cd weirdnames
    bfs_diff -regextype ed -regex '\./\((\)'
}

function test_regextype_emacs() {
    skip_unless invoke_bfs -regextype emacs -quit

    bfs_diff basic -regextype emacs -regex '.*/\(f+o?o?\|bar\)'
}

function test_regextype_grep() {
    skip_unless invoke_bfs -regextype grep -quit

    bfs_diff basic -regextype grep -regex '.*/f\+o\?o\?'
}

function test_regextype_sed() {
    cd weirdnames
    bfs_diff -regextype sed -regex '\./\((\)'
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
        $DIFF -u {"$TESTS","$TMP"}/test_s.out
    fi
}

function test_hidden() {
    bfs_diff weirdnames -hidden
}

function test_hidden_root() {
    cd weirdnames
    bfs_diff . ./. ... ./... .../.. -hidden
}

function test_nohidden() {
    bfs_diff weirdnames -nohidden
}

function test_nohidden_depth() {
    bfs_diff weirdnames -depth -nohidden
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

function test_mtime_units() {
    bfs_diff times -mtime +500w400d300h200m100s
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

function test_ls() {
    invoke_bfs rainbow -ls >scratch/test_ls.out
}

function test_L_ls() {
    invoke_bfs -L rainbow -ls >scratch/test_L_ls.out
}

function test_fls() {
    invoke_bfs rainbow -fls scratch/test_fls.out
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
    bfs_diff basic -maxdepth 0 -printf '%h\0%f\n'
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

    bfs_diff scratch -printf '(%p) (%l) %y %Y\n'
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

function test_printf_l_nonlink() {
    bfs_diff links -printf '| %26p -> %-26l |\n'
}

function test_printf_incomplete_escape() {
    fail invoke_bfs basic -printf '\'
}

function test_printf_invalid_escape() {
    fail invoke_bfs basic -printf '\!'
}

function test_printf_incomplete_format() {
    fail invoke_bfs basic -printf '%'
}

function test_printf_invalid_format() {
    fail invoke_bfs basic -printf '%!'
}

function test_printf_duplicate_flag() {
    fail invoke_bfs basic -printf '%--p'
}

function test_printf_must_be_numeric() {
    fail invoke_bfs basic -printf '%+p'
}

function test_printf_color() {
    bfs_diff -color -path './rainbow*' -printf '%H %h %f %p %P %l\n'
}

function test_fprintf() {
    invoke_bfs basic -fprintf scratch/test_fprintf.out '%%p(%p) %%d(%d) %%f(%f) %%h(%h) %%H(%H) %%P(%P) %%m(%m) %%M(%M) %%y(%y)\n'
    sort -o scratch/test_fprintf.out scratch/test_fprintf.out

    if [ "$UPDATE" ]; then
        cp scratch/test_fprintf.out "$TESTS/test_fprintf.out"
    else
        $DIFF -u "$TESTS/test_fprintf.out" scratch/test_fprintf.out
    fi
}

function test_fprintf_nofile() {
    fail invoke_bfs basic -fprintf
}

function test_fprintf_noformat() {
    fail invoke_bfs basic -fprintf /dev/null
}

function test_fstype() {
    fstype=$(invoke_bfs basic -maxdepth 0 -printf '%F\n')
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

function test_incomplete() {
    fail invoke_bfs basic \(
}

function test_missing_paren() {
    fail invoke_bfs basic \( -print
}

function test_extra_paren() {
    fail invoke_bfs basic -print \)
}

function test_color() {
    bfs_diff rainbow -color
}

function test_color_L() {
    bfs_diff -L rainbow -color
}

function test_color_rs_lc_rc_ec() {
    LS_COLORS="rs=RS:lc=LC:rc=RC:ec=EC:" bfs_diff rainbow -color
}

function test_color_escapes() {
    LS_COLORS="lc=\e[:rc=\155\::ec=^[\x5B\x6d:" bfs_diff rainbow -color
}

function test_color_nul() {
    LS_COLORS="ec=\33[m\0:" bfs_diff rainbow -color -maxdepth 0
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
    LS_COLORS="*.tar.gz=01;31:*.TAR=01;32:*.gz=01;33:" bfs_diff rainbow -color
}

function test_color_ext_underride() {
    LS_COLORS="*.gz=01;33:*.TAR=01;32:*.tar.gz=01;31:" bfs_diff rainbow -color
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
        $DIFF -u "$EXPECTED" "$ACTUAL"
    fi
}

function test_deep() {
    closefrom 4

    ulimit -n 16
    bfs_diff deep -type f -exec bash -c 'echo "${1:0:6}/.../${1##*/} (${#1})"' bash {} \;
}

function test_deep_strict() {
    closefrom 4

    # Not even enough fds to keep the root open
    ulimit -n 7
    bfs_diff deep -type f -exec bash -c 'echo "${1:0:6}/.../${1##*/} (${#1})"' bash {} \;
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

function test_exit_no_implicit_print() {
    bfs_diff basic -not -name foo -o -exit
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

function test_data_flow_group() {
    bfs_diff basic \( -group "$(id -g)" -nogroup \) -o \( -group "$(id -g)" -o -nogroup \)
}

function test_data_flow_user() {
    bfs_diff basic \( -user "$(id -u)" -nouser \) -o \( -user "$(id -u)" -o -nouser \)
}

function test_data_flow_hidden() {
    bfs_diff basic \( -hidden -not -hidden \) -o \( -hidden -o -not -hidden \)
}

function test_data_flow_sparse() {
    bfs_diff basic \( -sparse -not -sparse \) -o \( -sparse -o -not -sparse \)
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
    skip_unless test -e /dev/full
    fail invoke_bfs basic -maxdepth 0 >/dev/full
}

function test_fprint_error() {
    skip_unless test -e /dev/full
    fail invoke_bfs basic -maxdepth 0 -fprint /dev/full
}

function test_fprint_noerror() {
    # Regression test: /dev/full should not fail until actually written to
    skip_unless test -e /dev/full
    invoke_bfs basic -false -fprint /dev/full
}

function test_fprint_error_stdout() {
    skip_unless test -e /dev/full
    fail invoke_bfs basic -maxdepth 0 -fprint /dev/full >/dev/full
}

function test_fprint_error_stderr() {
    skip_unless test -e /dev/full
    fail invoke_bfs basic -maxdepth 0 -fprint /dev/full 2>/dev/full
}

function test_print0() {
    bfs_diff basic/a basic/b -print0
}

function test_fprint0() {
    invoke_bfs basic/a basic/b -fprint0 scratch/test_fprint0.out

    if [ "$UPDATE" ]; then
        cp scratch/test_fprint0.out "$TESTS/test_fprint0.out"
    else
        cmp -s scratch/test_fprint0.out "$TESTS/test_fprint0.out"
    fi
}

function test_closed_stdin() {
    bfs_diff basic <&-
}

function test_ok_closed_stdin() {
    bfs_diff basic -ok echo \; <&-
}

function test_okdir_closed_stdin() {
    bfs_diff basic -okdir echo {} \; <&-
}

function test_closed_stdout() {
    fail invoke_bfs basic >&-
}

function test_closed_stderr() {
    fail invoke_bfs basic >&- 2>&-
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
    skip_unless test "$SUDO"
    skip_if test "$UNAME" = "Darwin"

    rm -rf scratch/*
    mkdir scratch/{foo,mnt}
    sudo mount -t tmpfs tmpfs scratch/mnt
    $TOUCH scratch/foo/bar scratch/mnt/baz

    bfs_diff scratch -mount
    local ret=$?

    sudo umount scratch/mnt
    return $ret
}

function test_L_mount() {
    skip_unless test "$SUDO"
    skip_if test "$UNAME" = "Darwin"

    rm -rf scratch/*
    mkdir scratch/{foo,mnt}
    sudo mount -t tmpfs tmpfs scratch/mnt
    ln -s ../mnt scratch/foo/bar
    $TOUCH scratch/mnt/baz
    ln -s ../mnt/baz scratch/foo/qux

    bfs_diff -L scratch -mount
    local ret=$?

    sudo umount scratch/mnt
    return $ret
}

function test_xdev() {
    skip_unless test "$SUDO"
    skip_if test "$UNAME" = "Darwin"

    rm -rf scratch/*
    mkdir scratch/{foo,mnt}
    sudo mount -t tmpfs tmpfs scratch/mnt
    $TOUCH scratch/foo/bar scratch/mnt/baz

    bfs_diff scratch -xdev
    local ret=$?

    sudo umount scratch/mnt
    return $ret
}

function test_L_xdev() {
    skip_unless test "$SUDO"
    skip_if test "$UNAME" = "Darwin"

    rm -rf scratch/*
    mkdir scratch/{foo,mnt}
    sudo mount -t tmpfs tmpfs scratch/mnt
    ln -s ../mnt scratch/foo/bar
    $TOUCH scratch/mnt/baz
    ln -s ../mnt/baz scratch/foo/qux

    bfs_diff -L scratch -xdev
    local ret=$?

    sudo umount scratch/mnt
    return $ret
}

function test_inum_mount() {
    skip_unless test "$SUDO"
    skip_if test "$UNAME" = "Darwin"

    rm -rf scratch/*
    mkdir scratch/{foo,mnt}
    sudo mount -t tmpfs tmpfs scratch/mnt

    bfs_diff scratch -inum "$(inum scratch/mnt)"
    local ret=$?

    sudo umount scratch/mnt
    return $ret
}

function test_inum_bind_mount() {
    skip_unless test "$SUDO"
    skip_unless test "$UNAME" = "Linux"

    rm -rf scratch/*
    $TOUCH scratch/{foo,bar}
    sudo mount --bind scratch/{foo,bar}

    bfs_diff scratch -inum "$(inum scratch/bar)"
    local ret=$?

    sudo umount scratch/bar
    return $ret
}

function test_type_bind_mount() {
    skip_unless test "$SUDO"
    skip_unless test "$UNAME" = "Linux"

    rm -rf scratch/*
    $TOUCH scratch/{file,null}
    sudo mount --bind /dev/null scratch/null

    bfs_diff scratch -type c
    local ret=$?

    sudo umount scratch/null
    return $ret
}

function test_xtype_bind_mount() {
    skip_unless test "$SUDO"
    skip_unless test "$UNAME" = "Linux"

    rm -rf scratch/*
    $TOUCH scratch/{file,null}
    sudo mount --bind /dev/null scratch/null
    ln -s /dev/null scratch/link

    bfs_diff -L scratch -type c
    local ret=$?

    sudo umount scratch/null
    return $ret
}

function test_inum_automount() {
    # bfs shouldn't trigger automounts unless it descends into them

    skip_unless test "$SUDO"
    skip_unless command -v systemd-mount &>/dev/null

    rm -rf scratch/*
    mkdir scratch/{foo,mnt}
    skip_unless sudo systemd-mount -A -o bind basic scratch/mnt

    local before=$(inum scratch/mnt)
    bfs_diff scratch -inum "$before" -prune
    local ret=$?
    local after=$(inum scratch/mnt)

    sudo systemd-umount scratch/mnt

    ((ret == 0 && before == after))
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

    skip_unless invoke_bfs scratch -quit -acl

    $TOUCH scratch/{normal,acl}
    skip_unless set_acl scratch/acl
    ln -s acl scratch/link

    bfs_diff scratch -acl
}

function test_L_acl() {
    rm -rf scratch/*

    skip_unless invoke_bfs scratch -quit -acl

    $TOUCH scratch/{normal,acl}
    skip_unless set_acl scratch/acl
    ln -s acl scratch/link

    bfs_diff -L scratch -acl
}

function test_capable() {
    skip_unless test "$SUDO"
    skip_unless test "$UNAME" = "Linux"

    rm -rf scratch/*

    skip_unless invoke_bfs scratch -quit -capable

    $TOUCH scratch/{normal,capable}
    sudo setcap all+ep scratch/capable
    ln -s capable scratch/link

    bfs_diff scratch -capable
}

function test_L_capable() {
    skip_unless test "$SUDO"
    skip_unless test "$UNAME" = "Linux"

    rm -rf scratch/*

    skip_unless invoke_bfs scratch -quit -capable

    $TOUCH scratch/{normal,capable}
    sudo setcap all+ep scratch/capable
    ln -s capable scratch/link

    bfs_diff -L scratch -capable
}

function make_xattrs() {
    rm -rf scratch/*

    $TOUCH scratch/{normal,xattr,xattr_2}
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

function test_xattr() {
    skip_unless invoke_bfs scratch -quit -xattr
    skip_unless make_xattrs
    bfs_diff scratch -xattr
}

function test_L_xattr() {
    skip_unless invoke_bfs scratch -quit -xattr
    skip_unless make_xattrs
    bfs_diff -L scratch -xattr
}

function test_xattrname() {
    skip_unless invoke_bfs scratch -quit -xattr
    skip_unless make_xattrs

    case "$UNAME" in
        Darwin|FreeBSD)
            bfs_diff scratch -xattrname bfs_test
            ;;
        *)
            bfs_diff scratch -xattrname security.bfs_test
            ;;
    esac
}

function test_L_xattrname() {
    skip_unless invoke_bfs scratch -quit -xattr
    skip_unless make_xattrs

    case "$UNAME" in
        Darwin|FreeBSD)
            bfs_diff -L scratch -xattrname bfs_test
            ;;
        *)
            bfs_diff -L scratch -xattrname security.bfs_test
            ;;
    esac
}

function test_help() {
    invoke_bfs -help | grep -E '\{...?\}' && return 1
    invoke_bfs -D help | grep -E '\{...?\}' && return 1
    invoke_bfs -S help | grep -E '\{...?\}' && return 1
    invoke_bfs -regextype help | grep -E '\{...?\}' && return 1

    return 0
}

function test_version() {
    invoke_bfs -version >/dev/null
}

function test_typo() {
    invoke_bfs -dikkiq 2>&1 | grep follow >/dev/null
}

function test_D_multi() {
    bfs_diff -D opt,tree,unknown basic
}

function test_D_all() {
    bfs_diff -D all basic
}

function test_O0() {
    bfs_diff -O0 basic -not \( -type f -not -type f \)
}

function test_O1() {
    bfs_diff -O1 basic -not \( -type f -not -type f \)
}

function test_O2() {
    bfs_diff -O2 basic -not \( -type f -not -type f \)
}

function test_O3() {
    bfs_diff -O3 basic -not \( -type f -not -type f \)
}

function test_Ofast() {
    bfs_diff -Ofast basic -not \( -xtype f -not -xtype f \)
}

function test_S() {
    invoke_bfs -S "$1" -s basic >"scratch/test_S_$1.out"

    if [ "$UPDATE" ]; then
        cp {scratch,"$TESTS"}/"test_S_$1.out"
    else
        $DIFF -u {"$TESTS",scratch}/"test_S_$1.out"
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

function test_exclude_name() {
    bfs_diff basic -exclude -name foo
}

function test_exclude_depth() {
    bfs_diff basic -depth -exclude -name foo
}

function test_exclude_mindepth() {
    bfs_diff basic -mindepth 3 -exclude -name foo
}

function test_exclude_print() {
    fail invoke_bfs basic -exclude -print
}

function test_exclude_exclude() {
    fail invoke_bfs basic -exclude -exclude -name foo
}

function test_flags() {
    skip_unless invoke_bfs scratch -quit -flags offline

    rm -rf scratch/*

    $TOUCH scratch/{foo,bar}
    skip_unless chflags offline scratch/bar

    bfs_diff scratch -flags -offline,nohidden
}

function test_files0_from_file() {
    cd weirdnames
    invoke_bfs -mindepth 1 -fprintf ../scratch/files0.in "%P\0"
    bfs_diff -files0-from ../scratch/files0.in
}

function test_files0_from_stdin() {
    cd weirdnames
    invoke_bfs -mindepth 1 -printf "%P\0" | bfs_diff -files0-from -
}

function test_files0_from_none() {
    printf "" | fail invoke_bfs -files0-from -
}

function test_files0_from_empty() {
    printf "\0" | fail invoke_bfs -files0-from -
}

function test_files0_from_nowhere() {
    fail invoke_bfs -files0-from
}

function test_files0_from_nothing() {
    fail invoke_bfs -files0-from basic/nonexistent
}

function test_files0_from_ok() {
    printf "basic\0" | fail invoke_bfs -files0-from - -ok echo {} \;
}

function test_stderr_fails_silently() {
    skip_unless test -e /dev/full
    bfs_diff -D all basic 2>/dev/full
}

function test_stderr_fails_loudly() {
    skip_unless test -e /dev/full
    fail invoke_bfs -D all basic -false -fprint /dev/full 2>/dev/full
}

function test_unexpected_operator() {
    fail invoke_bfs \! -o -print
}

BOL=
EOL='\n'

function update_eol() {
    # Put the cursor at the last column, then write a space so the next
    # character will wrap
    EOL="\\033[${COLUMNS}G "
}

if [[ -t 1 && ! "$VERBOSE_TESTS" ]]; then
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

for test in "${enabled_tests[@]}"; do
    printf "${BOL}${YLW}%s${RST}${EOL}" "$test"

    if [ "$VERBOSE_ERRORS" ]; then
        ("$test")
    else
        ("$test") 2>"$TMP/stderr"
    fi
    status=$?

    if ((status == 0)); then
        ((++passed))
    elif ((status == EX_SKIP)); then
        ((++skipped))
        if [ "$VERBOSE_SKIPPED" ]; then
            printf "${BOL}${CYN}%s skipped!${RST}\n" "$test"
        fi
    else
        ((++failed))
        [ "$VERBOSE_ERRORS" ] || cat "$TMP/stderr" >&2
        printf "${BOL}${RED}%s failed!${RST}\n" "$test"
        [ "$STOP" ] && break
    fi
done

if ((passed > 0)); then
    printf "${BOL}${GRN}tests passed: %d${RST}\n" "$passed"
fi
if ((skipped > 0)); then
    printf "${BOL}${CYN}tests skipped: %s${RST}\n" "$skipped"
fi
if ((failed > 0)); then
    printf "${BOL}${RED}tests failed: %s${RST}\n" "$failed"
    exit 1
fi
