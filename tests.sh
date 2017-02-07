#!/bin/bash

set -o physical
umask 022

export LC_ALL=C

# The temporary directory that will hold our test data
TMP="$(mktemp -d "${TMPDIR:-/tmp}"/bfs.XXXXXXXXXX)"
chown "$(id -u)":"$(id -g)" "$TMP"

# Clean up temporary directories on exit
function cleanup() {
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
    touchp "$1/a"
    ln -s a "$1/b"
    ln "$1/a" "$1/c"
    mkdir -p "$1/d/e/f"
    ln -s ../../d "$1/d/e/g"
    ln -s d/e "$1/h"
    ln -s q "$1/d/e/i"
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
}
make_weirdnames "$TMP/weirdnames"

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

BSD=yes
GNU=yes
ALL=yes

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
        --update)
            UPDATE=yes
            ;;
        *)
            echo "Unrecognized option '$arg'." >&2
            exit 1
            ;;
    esac
done

function bfs_sort() {
    awk -F/ '{ print NF - 1 " " $0 }' | sort -n | cut -d' ' -f2-
}

function bfs_diff() {
    local OUT="$TESTS/${FUNCNAME[1]}.out"
    if [ "$UPDATE" ]; then
        $BFS "$@" | bfs_sort >"$OUT"
    else
        diff -u "$OUT" <($BFS "$@" | bfs_sort)
    fi
}

cd "$TMP"

# Test cases

function test_0001() {
    bfs_diff basic
}

function test_0002() {
    bfs_diff basic -type d
}

function test_0003() {
    bfs_diff basic -type f
}

function test_0004() {
    bfs_diff basic -mindepth 1
}

function test_0005() {
    bfs_diff basic -maxdepth 1
}

function test_0006() {
    bfs_diff basic -mindepth 1 -depth
}

function test_0007() {
    bfs_diff basic -mindepth 2 -depth
}

function test_0008() {
    bfs_diff basic -maxdepth 1 -depth
}

function test_0009() {
    bfs_diff basic -maxdepth 2 -depth
}

function test_0010() {
    bfs_diff basic -name '*f*'
}

function test_0011() {
    bfs_diff basic -path 'basic/*f*'
}

function test_0012() {
    [ "$GNU" ] || return 0
    bfs_diff perms -executable
}

function test_0013() {
    [ "$GNU" ] || return 0
    bfs_diff perms -readable
}

function test_0014() {
    [ "$GNU" ] || return 0
    bfs_diff perms -writable
}

function test_0015() {
    [ "$GNU" ] || return 0
    bfs_diff basic -empty
}

function test_0016() {
    [ "$GNU" ] || return 0
    bfs_diff basic -gid "$(id -g)"
}

function test_0017() {
    [ "$GNU" ] || return 0
    bfs_diff basic -gid +0
}

function test_0018() {
    [ "$GNU" ] || return 0
    bfs_diff basic -gid "-$(($(id -g) + 1))"
}

function test_0019() {
    [ "$GNU" ] || return 0
    bfs_diff basic -uid "$(id -u)"
}

function test_0020() {
    [ "$GNU" ] || return 0
    bfs_diff basic -uid +0
}

function test_0021() {
    [ "$GNU" ] || return 0
    bfs_diff basic -uid "-$(($(id -u) + 1))"
}

function test_0022() {
    bfs_diff times -newer times/a
}

function test_0023() {
    [ "$GNU" ] || return 0
    bfs_diff times -anewer times/a
}

function test_0024() {
    bfs_diff links -type f -links 2
}

function test_0025() {
    bfs_diff links -type f -links -2
}

function test_0026() {
    bfs_diff links -type f -links +1
}

function test_0027() {
    bfs_diff -P links/d/e/f
}

function test_0028() {
    bfs_diff -P links/d/e/f/
}

function test_0029() {
    bfs_diff -H links/d/e/f
}

function test_0030() {
    bfs_diff -H links/d/e/f/
}

function test_0031() {
    bfs_diff -H times -newer times/l
}

function test_0032() {
    bfs_diff -H links/d/e/i
}

function test_0033() {
    bfs_diff -L links 2>/dev/null
}

function test_0034() {
    [ "$BSD" -o "$GNU" ] || return 0
    bfs_diff links -follow 2>/dev/null
}

function test_0035() {
    bfs_diff -L links -depth 2>/dev/null
}

function test_0036() {
    [ "$BSD" -o "$GNU" ] || return 0
    bfs_diff links -samefile links/a
}

function test_0037() {
    [ "$GNU" ] || return 0
    bfs_diff links -xtype l
}

function test_0038() {
    [ "$GNU" ] || return 0
    bfs_diff links -xtype f
}

function test_0039() {
    [ "$GNU" ] || return 0
    bfs_diff -L links -xtype l 2>/dev/null
}

function test_0040() {
    [ "$GNU" ] || return 0
    bfs_diff -L links -xtype f 2>/dev/null
}

function test_0041() {
    bfs_diff basic/a -name a
}

function test_0042() {
    bfs_diff basic/g/ -name g
}

function test_0043() {
    [ "$BSD" -o "$GNU" ] || return 0
    bfs_diff / -maxdepth 0 -name / 2>/dev/null
}

function test_0044() {
    [ "$BSD" -o "$GNU" ] || return 0
    bfs_diff /// -maxdepth 0 -name / 2>/dev/null
}

function test_0045() {
    [ "$BSD" -o "$GNU" ] || return 0
    bfs_diff basic -iname '*F*'
}

function test_0046() {
    [ "$BSD" -o "$GNU" ] || return 0
    bfs_diff basic -ipath 'basic/*F*'
}

function test_0047() {
    [ "$BSD" -o "$GNU" ] || return 0
    bfs_diff links -lname '[aq]'
}

function test_0048() {
    [ "$BSD" -o "$GNU" ] || return 0
    bfs_diff links -ilname '[AQ]'
}

function test_0049() {
    [ "$BSD" -o "$GNU" ] || return 0
    bfs_diff -L links -lname '[aq]' 2>/dev/null
}

function test_0050() {
    [ "$BSD" -o "$GNU" ] || return 0
    bfs_diff -L links -ilname '[AQ]' 2>/dev/null
}

function test_0051() {
    bfs_diff -L basic -user "$(id -un)"
}

function test_0052() {
    bfs_diff -L basic -user "$(id -u)"
}

function test_0053() {
    bfs_diff -L basic -group "$(id -gn)"
}

function test_0054() {
    bfs_diff -L basic -group "$(id -g)"
}

function test_0055() {
    [ "$GNU" ] || return 0
    bfs_diff basic -daystart -mtime 0
}

function test_0056() {
    [ "$GNU" ] || return 0
    bfs_diff basic -daystart -daystart -mtime 0
}

function test_0057() {
    [ "$BSD" -o "$GNU" ] || return 0
    bfs_diff times -newerma times/a
}

function test_0058() {
    bfs_diff basic -type f -size 0
}

function test_0059() {
    bfs_diff basic -type f -size +0
}

function test_0060() {
    bfs_diff basic -type f -size +0c
}

function test_0061() {
    [ "$BSD" -o "$GNU" ] || return 0
    bfs_diff basic -size 9223372036854775807
}

function test_0062() {
    bfs_diff basic -exec echo '{}' ';'
}

function test_0063() {
    [ "$BSD" -o "$GNU" ] || return 0
    bfs_diff basic -exec echo '-{}-' ';'
}

function test_0064() {
    [ "$BSD" -o "$GNU" ] || return 0

    local TMP_REAL="$(cd "$TMP" && pwd)"
    local OFFSET="$((${#TMP_REAL} + 2))"
    bfs_diff basic -execdir bash -c "pwd | cut -b$OFFSET-" ';'
}

function test_0065() {
    [ "$GNU" ] || return 0
    bfs_diff basic -execdir echo '{}' ';'
}

function test_0066() {
    [ "$GNU" ] || return 0
    bfs_diff basic -execdir echo '-{}-' ';'
}

function test_0067() {
    bfs_diff basic \( -name '*f*' \)
}

function test_0068() {
    [ "$GNU" ] || return 0
    bfs_diff basic -name '*f*' -print , -print
}

function test_0069() {
    [ "$GNU" ] || return 0
    cd weirdnames
    bfs_diff '-' '(-' '!-' ',' ')' './(' './!' \( \! -print , -print \)
}

function test_0070() {
    [ "$GNU" ] || return 0
    cd weirdnames
    bfs_diff -L '-' '(-' '!-' ',' ')' './(' './!' \( \! -print , -print \)
}

function test_0071() {
    cd weirdnames
    bfs_diff -L ',' -print
}

function test_0072() {
    [ "$GNU" ] || return 0
    cd weirdnames
    bfs_diff -follow ',' -print
}

function test_0073() {
    [ "$GNU" ] || return 0
    if [ "$UPDATE" ]; then
        $BFS basic -fprint "$TESTS/test_0073.out"
        sort -o "$TESTS/test_0073.out" "$TESTS/test_0073.out"
    else
        $BFS basic -fprint scratch/test_0073.out
        sort -o scratch/test_0073.out scratch/test_0073.out
        diff -u scratch/test_0073.out "$TESTS/test_0073.out"
    fi
}

function test_0074() {
    [ "$BSD" -o "$GNU" ] || return 0
    cd basic
    bfs_diff -- . -type f
}

function test_0075() {
    [ "$BSD" -o "$GNU" ] || return 0
    cd basic
    bfs_diff -L -- . -type f
}

function test_0076() {
    [ "$GNU" ] || return 0

    # Make sure -ignore_readdir_race doesn't suppress ENOENT at the root
    ! $BFS basic/nonexistent -ignore_readdir_race 2>/dev/null
}

function test_0077() {
    [ "$GNU" ] || return 0

    rm -rf scratch/*
    touch scratch/{foo,bar}

    # -links 1 forces a stat() call, which will fail for the second file
    $BFS scratch -mindepth 1 -ignore_readdir_race -links 1 -exec "$TESTS/remove-sibling.sh" '{}' ';'
}

function test_0078() {
    bfs_diff perms -perm 222
}

function test_0079() {
    bfs_diff perms -perm -222
}

function test_0080() {
    [ "$GNU" ] || return 0
    bfs_diff perms -perm /222
}

function test_0081() {
    bfs_diff perms -perm 644
}

function test_0082() {
    bfs_diff perms -perm -644
}

function test_0083() {
    [ "$GNU" ] || return 0
    bfs_diff perms -perm /644
}

function test_0084() {
    bfs_diff perms -perm a+r,u=wX,g+wX-w
}

function test_0085() {
    bfs_diff perms -perm -a+r,u=wX,g+wX-w
}

function test_0086() {
    [ "$GNU" ] || return 0
    bfs_diff perms -perm /a+r,u=wX,g+wX-w
}

function test_0087() {
    [ "$ALL" ] || return 0
    ! $BFS perms -perm a+r, 2>/dev/null
}

function test_0088() {
    [ "$ALL" ] || return 0
    ! $BFS perms -perm a+r,,u+w 2>/dev/null
}

function test_0089() {
    [ "$ALL" ] || return 0
    ! $BFS perms -perm a 2>/dev/null
}

function test_0090() {
    bfs_diff perms -perm -+rwx
}

function test_0091() {
    [ "$GNU" ] || return 0
    bfs_diff perms -perm /+rwx
}

function test_0092() {
    [ "$ALL" ] || return 0
    bfs_diff perms -perm +rwx
}

function test_0093() {
    [ "$ALL" ] || return 0
    ! $BFS perms -perm +777 2>/dev/null
}

function test_0094() {
    [ "$GNU" ] || return 0
    # -ok should close stdin for the executed command
    yes | $BFS basic -ok cat ';' 2>/dev/null
}

function test_0095() {
    [ "$GNU" ] || return 0
    # -okdir should close stdin for the executed command
    yes | $BFS basic -okdir cat ';' 2>/dev/null
}

function test_0096() {
    bfs_diff basic/ -depth
}

function test_0097() {
    [ "$BSD" -o "$GNU" ] || return 0
    # Don't try to delete '.'
    (cd scratch && $BFS . -delete)
}

function test_0098() {
    [ "$BSD" -o "$GNU" ] || return 0
    bfs_diff / -maxdepth 0 -execdir pwd ';'
}

function test_0099() {
    [ "$BSD" -o "$GNU" ] || return 0
    # Don't prepend ./ for absolute paths in -execdir
    bfs_diff / -maxdepth 0 -execdir echo '{}' ';'
}

function test_0100() {
    [ "$ALL" ] || return 0
    bfs_diff /// -maxdepth 0 -execdir echo '{}' ';'
}

function test_0101() {
    [ "$BSD" -o "$GNU" ] || return 0
    bfs_diff basic -regex 'basic/./.'
}

function test_0102() {
    [ "$BSD" -o "$GNU" ] || return 0
    bfs_diff basic -iregex 'basic/[A-Z]/[a-z]'
}

function test_0103() {
    [ "$BSD" -o "$GNU" ] || return 0
    cd weirdnames
    bfs_diff . -regex '\./\((\)'
}

function test_0104() {
    [ "$BSD" ] || return 0
    cd weirdnames
    bfs_diff -E . -regex '\./(\()'
}

function test_0105() {
    [ "$GNU" ] || return 0
    cd weirdnames
    bfs_diff -regextype posix-basic -regex '\./\((\)'
}

function test_0106() {
    [ "$GNU" ] || return 0
    cd weirdnames
    bfs_diff -regextype posix-extended -regex '\./(\()'
}

function test_0107() {
    [ "$BSD" ] || return 0
    bfs_diff -d basic
}

function test_0108() {
    [ "$GNU" ] || return 0
    bfs_diff basic -d 2>/dev/null
}

function test_0109() {
    [ "$BSD" ] || return 0
    cd weirdnames
    bfs_diff -f '-' -f '('
}

function test_0110() {
    [ "$ALL" ] || return 0
    bfs_diff weirdnames -hidden
}

function test_0111() {
    [ "$ALL" ] || return 0
    bfs_diff weirdnames -nohidden
}

function test_0112() {
    [ "$BSD" ] || return 0
    bfs_diff basic -depth 2
}

function test_0113() {
    [ "$BSD" ] || return 0
    bfs_diff basic -depth +2
}

function test_0114() {
    [ "$BSD" ] || return 0
    bfs_diff basic -depth -2
}

function test_0115() {
    [ "$BSD" ] || return 0
    bfs_diff basic -depth -depth 2
}

function test_0116() {
    [ "$BSD" ] || return 0
    bfs_diff basic -depth -depth +2
}

function test_0117() {
    [ "$BSD" ] || return 0
    bfs_diff basic -depth -depth -2
}

function test_0118() {
    [ "$BSD" ] || return 0
    bfs_diff basic -gid "$(id -gn)"
}

function test_0119() {
    [ "$BSD" ] || return 0
    bfs_diff basic -uid "$(id -un)"
}

function test_0120() {
    [ "$BSD" ] || return 0
    bfs_diff times -mnewer times/a
}

function test_0121() {
    [ "$BSD" ] || return 0
    bfs_diff -H times -mnewer times/l
}

function test_0122() {
    [ "$BSD" ] || return 0
    bfs_diff basic -type f -size 1T
}

function test_0123() {
    [ "$BSD" -o "$GNU" ] || return 0
    bfs_diff basic/g -print -name g -quit
}

function test_0124() {
    [ "$BSD" -o "$GNU" ] || return 0
    bfs_diff basic/g -print -name h -quit
}

function test_0125() {
    [ "$BSD" -o "$GNU" ] || return 0
    bfs_diff basic/g -depth -print -name g -quit
}

function test_0126() {
    [ "$BSD" -o "$GNU" ] || return 0
    bfs_diff basic/g -depth -print -name h -quit
}

function test_0127() {
    [ "$BSD" -o "$GNU" ] || return 0

    local inode="$(ls -id basic/k/foo/bar | cut -f1 -d' ')"
    bfs_diff basic -inum "$inode"
}

function test_0128() {
    [ "$BSD" -o "$GNU" ] || return 0
    bfs_diff basic -nogroup
}

function test_0129() {
    [ "$BSD" -o "$GNU" ] || return 0
    bfs_diff basic -nouser
}

function test_0130() {
    [ "$GNU" ] || return 0
    bfs_diff basic -printf '%%p(%p) %%d(%d) %%f(%f) %%h(%h) %%H(%H) %%P(%P) %%m(%m) %%M(%M) %%y(%y)\n'
}

function test_0131() {
    [ "$GNU" ] || return 0
    bfs_diff / -maxdepth 0 -printf '(%h)/(%f)\n'
}

function test_0132() {
    [ "$GNU" ] || return 0
    bfs_diff /// -maxdepth 0 -printf '(%h)/(%f)\n'
}

function test_0133() {
    [ "$GNU" ] || return 0
    bfs_diff basic/ -printf '(%h)/(%f)\n'
}

function test_0134() {
    [ "$GNU" ] || return 0
    bfs_diff basic/// -printf '(%h)/(%f)\n'
}

function test_0135() {
    [ "$GNU" ] || return 0
    bfs_diff basic -printf '|%- 10.10p| %+03d %#4m\n'
}

function test_0136() {
    [ "$GNU" ] || return 0
    bfs_diff links -printf '(%p) (%l) %y %Y\n'
}

function test_0137() {
    [ "$GNU" ] || return 0
    bfs_diff basic -maxdepth 0 -printf '\18\118\1118\11118\n\cfoo'
}

function test_0138() {
    bfs_diff basic/g -depth -name g
}

function test_0139() {
    [ "$BSD" -o "$GNU" ] || return 0
    bfs_diff basic basic -print -quit
}

function test_0140() {
    [ "$BSD" -o "$GNU" ] || return 0
    bfs_diff basic basic -quit -print
}

result=0

for i in {1..140}; do
    test="test_$(printf '%04d' $i)"

    if [ -t 1 ]; then
        printf '\r%s' "$test"
    fi

    ("$test" "$dir")
    status=$?

    if [ $status -ne 0 ]; then
        if [ -t 1 ]; then
            printf '\r%s failed!\n' "$test"
        else
            printf '%s failed!\n' "$test"
        fi
        result=$status
    fi
done

if [ -t 1 ]; then
    printf '\n'
fi

exit $result
