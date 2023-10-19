#!/usr/bin/env bash

# Copyright © Tavian Barnes <tavianator@tavianator.com>
# SPDX-License-Identifier: 0BSD

set -euP
umask 022

TESTS="$(dirname -- "${BASH_SOURCE[0]}")"
. "$TESTS/util.sh"
. "$TESTS/color.sh"
. "$TESTS/stddirs.sh"
. "$TESTS/getopts.sh"
. "$TESTS/run.sh"

stdenv
drop_root "$@"
parse_args "$@"
make_stddirs
run_tests
