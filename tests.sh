#!/bin/bash

# The temporary directory that will hold our test data
TMP="$(mktemp -d "${TMPDIR:-/tmp}"/bfs.XXXXXXXXXX)"
chown "$(id -u)":"$(id -g)" "$TMP"

# Clean up temporary directories on exit
function cleanup() {
    rm -rf "$TMP"
}
trap cleanup EXIT

# Like a mythical touch -p
function touchp() {
    install -Dm644 /dev/null "$1"
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
}
make_basic "$TMP/basic"

# Creates a file+directory structure with various permissions for tests
function make_perms() {
    install -Dm444 /dev/null "$1/r"
    install -Dm222 /dev/null "$1/w"
    install -Dm644 /dev/null "$1/rw"
    install -Dm555 /dev/null "$1/rx"
    install -Dm311 /dev/null "$1/wx"
    install -Dm755 /dev/null "$1/rwx"
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

if [ "$1" == "update" ]; then
    UPDATE="update"
fi

function bfs_sort() {
    (
        export LC_ALL=C
        awk -F/ '{ print NF - 1 " " $0 }' | sort -n | awk '{ print $2 }'
    )
}

function bfs_diff() {
    local OUT="$TESTS/${FUNCNAME[1]}.out"
    if [ "$UPDATE" ]; then
        "$BFS" "$@" | bfs_sort >"$OUT"
    else
        diff -u "$OUT" <("$BFS" "$@" | bfs_sort)
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
    bfs_diff perms -executable
}

function test_0013() {
    bfs_diff perms -readable
}

function test_0014() {
    bfs_diff perms -writable
}

function test_0015() {
    bfs_diff basic -empty
}

function test_0016() {
    bfs_diff basic -gid "$(id -g)"
}

function test_0017() {
    bfs_diff basic -gid +0
}

function test_0018() {
    bfs_diff basic -gid "-$(($(id -g) + 1))"
}

function test_0019() {
    bfs_diff basic -uid "$(id -u)"
}

function test_0020() {
    bfs_diff basic -uid +0
}

function test_0021() {
    bfs_diff basic -uid "-$(($(id -u) + 1))"
}

function test_0022() {
    bfs_diff times -newer times/a
}

function test_0023() {
    bfs_diff times -anewer times/a
}

function test_0024() {
    bfs_diff links -links 2
}

function test_0025() {
    bfs_diff links -links -2
}

function test_0026() {
    bfs_diff links -links +1
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
    bfs_diff links -follow 2>/dev/null
}

function test_0035() {
    bfs_diff -L links -depth 2>/dev/null
}

function test_0036() {
    bfs_diff links -samefile links/a
}

function test_0037() {
    bfs_diff links -xtype l
}

function test_0038() {
    bfs_diff links -xtype f
}

function test_0039() {
    bfs_diff -L links -xtype l 2>/dev/null
}

function test_0040() {
    bfs_diff -L links -xtype f 2>/dev/null
}

function test_0041() {
    bfs_diff basic/a -name a
}

function test_0042() {
    bfs_diff basic/g/ -name g
}

function test_0043() {
    bfs_diff / -maxdepth 0 -name / 2>/dev/null
}

function test_0044() {
    bfs_diff // -maxdepth 0 -name / 2>/dev/null
}

function test_0045() {
    bfs_diff basic -iname '*F*'
}

function test_0046() {
    bfs_diff basic -ipath 'basic/*F*'
}

function test_0047() {
    bfs_diff links -lname '[aq]'
}

function test_0048() {
    bfs_diff links -ilname '[AQ]'
}

function test_0049() {
    bfs_diff -L links -lname '[aq]' 2>/dev/null
}

function test_0050() {
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
    bfs_diff basic -daystart -mtime 0
}

function test_0056() {
    bfs_diff basic -daystart -daystart -mtime 0
}

function test_0057() {
    bfs_diff times -newerma times/a
}

function test_0058() {
    bfs_diff basic -size 0
}

function test_0059() {
    bfs_diff basic -size +0
}

function test_0060() {
    bfs_diff basic -size +0c
}

function test_0061() {
    bfs_diff basic -size 9223372036854775807
}

function test_0062() {
    bfs_diff basic -exec echo '{}' ';'
}

function test_0063() {
    bfs_diff basic -exec echo '-{}-' ';'
}

function test_0064() {
    local OFFSET="$((${#TMP} + 2))"
    bfs_diff basic -execdir bash -c "pwd | cut -b$OFFSET-" ';'
}

function test_0065() {
    bfs_diff basic -execdir echo '{}' ';'
}

function test_0066() {
    bfs_diff basic -execdir echo '-{}-' ';'
}

function test_0067() {
    bfs_diff basic \( -name '*f*' \)
}

function test_0068() {
    bfs_diff basic -name '*f*' -print , -print
}

function test_0069() {
    cd weirdnames
    bfs_diff '-' '(-' '!-' ',' ')' './(' './!' \( \! -print , -print \)
}

function test_0070() {
    cd weirdnames
    bfs_diff -L '-' '(-' '!-' ',' ')' './(' './!' \( \! -print , -print \)
}

function test_0071() {
    cd weirdnames
    bfs_diff -L ',' -true
}

function test_0072() {
    cd weirdnames
    bfs_diff -follow ',' -true
}

function test_0073() {
    if [ "$UPDATE" ]; then
        "$BFS" basic -fprint "$TESTS/test_0073.out"
        sort -o "$TESTS/test_0073.out" "$TESTS/test_0073.out"
    else
        "$BFS" basic -fprint scratch/test_0073.out
        sort -o scratch/test_0073.out scratch/test_0073.out
        diff -u scratch/test_0073.out "$TESTS/test_0073.out"
    fi
}

function test_0074() {
    cd basic
    bfs_diff -- -type f
}

function test_0075() {
    cd basic
    bfs_diff -L -- -type f
}

function test_0076() {
    # Make sure -ignore_readdir_race doesn't suppress ENOENT at the root
    ! "$BFS" basic/nonexistent -ignore_readdir_race 2>/dev/null
}

function test_0077() {
    rm -rf scratch/*
    touch scratch/{foo,bar}

    # -links 1 forces a stat() call, which will fail for the second file
    "$BFS" scratch -mindepth 1 -ignore_readdir_race -links 1 -exec "$TESTS/remove-sibling.sh" '{}' ';'
}

function test_0078() {
    bfs_diff perms -perm 222
}

function test_0079() {
    bfs_diff perms -perm -222
}

function test_0080() {
    bfs_diff perms -perm /222
}

function test_0081() {
    bfs_diff perms -perm 644
}

function test_0082() {
    bfs_diff perms -perm -644
}

function test_0083() {
    bfs_diff perms -perm /644
}

function test_0084() {
    bfs_diff perms -perm a+r,u=wX,g+wX-w
}

function test_0085() {
    bfs_diff perms -perm -a+r,u=wX,g+wX-w
}

function test_0086() {
    bfs_diff perms -perm /a+r,u=wX,g+wX-w
}

function test_0087() {
    ! "$BFS" perms -perm a+r, 2>/dev/null
}

function test_0088() {
    ! "$BFS" perms -perm a+r,,u+w 2>/dev/null
}

function test_0089() {
    ! "$BFS" perms -perm a 2>/dev/null
}

function test_0090() {
    bfs_diff perms -perm -+rwx
}

function test_0091() {
    bfs_diff perms -perm /+rwx
}

function test_0092() {
    bfs_diff perms -perm +rwx
}

function test_0093() {
    ! "$BFS" perms -perm +777 2>/dev/null
}

function test_0094() {
    # -ok should close stdin for the executed command
    yes | "$BFS" basic -ok cat ';' 2>/dev/null
}

function test_0095() {
    # -okdir should close stdin for the executed command
    yes | "$BFS" basic -okdir cat ';' 2>/dev/null
}

function test_0096() {
    bfs_diff basic/ -depth
}

function test_0097() {
    # Don't try to delete '.'
    (cd scratch && "$BFS" -delete)
}

function test_0098() {
    bfs_diff / -maxdepth 0 -execdir pwd ';'
}

function test_0099() {
    # Don't prepend ./ for absolute paths in -execdir
    bfs_diff / -maxdepth 0 -execdir echo '{}' ';'
}

function test_0100() {
    # // is canonicalized to /
    bfs_diff // -maxdepth 0 -execdir echo '{}' ';'
}

for i in {1..100}; do
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
        exit $status
    fi
done

if [ -t 1 ]; then
    printf '\n'
fi
