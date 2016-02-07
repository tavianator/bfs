#!/bin/bash

# Like a mythical touch -p
function touchp() {
    install -Dm644 /dev/null "$1"
}

# Creates a simple file+directory structure for tests
function basic_structure() {
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

# Creates a file+directory structure with various permissions for tests
function perms_structure() {
    install -Dm444 /dev/null "$1/r"
    install -Dm222 /dev/null "$1/w"
    install -Dm644 /dev/null "$1/rw"
    install -Dm555 /dev/null "$1/rx"
    install -Dm311 /dev/null "$1/wx"
    install -Dm755 /dev/null "$1/rwx"
}

# Creates a file+directory structure with various symbolic and hard links
function links_structure() {
    touchp "$1/a"
    ln -s a "$1/b"
    ln "$1/a" "$1/c"
    mkdir -p "$1/d/e"
    ln -s ../../d "$1/d/e/f"
    touchp "$1/d/e/g"
    ln -s q "$1/d/e/h"
}

# Checks for any (order-independent) differences between bfs and find
function find_diff() {
    diff -u <(./bfs "$@" | sort) <(find "$@" | sort)
}

# Test cases

function test_0001() {
    basic_structure "$1"
    find_diff "$1"
}

function test_0002() {
    basic_structure "$1"
    find_diff "$1" -type d
}

function test_0003() {
    basic_structure "$1"
    find_diff "$1" -type f
}

function test_0004() {
    basic_structure "$1"
    find_diff "$1" -mindepth 1
}

function test_0005() {
    basic_structure "$1"
    find_diff "$1" -maxdepth 1
}

function test_0006() {
    basic_structure "$1"
    find_diff "$1" -mindepth 1 -depth
}

function test_0007() {
    basic_structure "$1"
    find_diff "$1" -mindepth 2 -depth
}

function test_0008() {
    basic_structure "$1"
    find_diff "$1" -maxdepth 1 -depth
}

function test_0009() {
    basic_structure "$1"
    find_diff "$1" -maxdepth 2 -depth
}

function test_0010() {
    basic_structure "$1"
    find_diff "$1" -name '*f*'
}

function test_0011() {
    basic_structure "$1"
    find_diff "$1" -path "$1/*f*"
}

function test_0012() {
    perms_structure "$1"
    find_diff "$1" -executable
}

function test_0013() {
    perms_structure "$1"
    find_diff "$1" -readable
}

function test_0014() {
    perms_structure "$1"
    find_diff "$1" -writable
}

function test_0015() {
    basic_structure "$1"
    find_diff "$1" -empty
}

function test_0016() {
    basic_structure "$1"
    find_diff "$1" -gid "$(id -g)" && \
        find_diff "$1" -gid +0 && \
        find_diff "$1" -gid -10000
}

function test_0017() {
    basic_structure "$1"
    find_diff "$1" -uid "$(id -u)" && \
        find_diff "$1" -uid +0 && \
        find_diff "$1" -uid -10000
}

function test_0018() {
    basic_structure "$1"
    find_diff "$1" -newer "$1/e/f"
}

function test_0019() {
    basic_structure "$1"
    find_diff "$1" -cnewer "$1/e/f"
}

function test_0020() {
    links_structure "$1"
    find_diff "$1" -links 2 && \
        find_diff "$1" -links -2 && \
        find_diff "$1" -links +1
}

function test_0021() {
    links_structure "$1"
    find_diff -P "$1/d/e/f" && \
        find_diff -P "$1/d/e/f/"
}

function test_0022() {
    links_structure "$1"
    find_diff -H "$1/d/e/f" && \
        find_diff -H "$1/d/e/f/"
}

function test_0023() {
    links_structure "$1"
    find_diff -H "$1" -newer "$1/d/e/f"
}

function test_0024() {
    links_structure "$1"
    find_diff -H "$1/d/e/h"
}

for i in {1..24}; do
    dir="$(mktemp -d "${TMPDIR:-/tmp}"/bfs.XXXXXXXXXX)"
    test="test_$(printf '%04d' $i)"
    "$test" "$dir"
    status=$?
    rm -rf "$dir"
    if [ $status -ne 0 ]; then
        echo "$test failed!" >&2
        exit $status
    fi
done
