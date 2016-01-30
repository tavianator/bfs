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

for i in {1..15}; do
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
