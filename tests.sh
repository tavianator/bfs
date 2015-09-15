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
    find_diff "$1" -maxdepth 1 -depth
}

for i in {1..7}; do
    dir="$(mktemp -d)"
    test="test_$(printf '%04d' $i)"
    "$test" "$dir"
    status=$?
    rm -rf "$dir"
    if [ $status -ne 0 ]; then
        echo "$test failed!" >&2
        exit $status
    fi
done
