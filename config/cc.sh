#!/usr/bin/env bash

# Copyright © Tavian Barnes <tavianator@tavianator.com>
# SPDX-License-Identifier: 0BSD

# Run the compiler and check if it succeeded

printf '$ %s' "$XCC" >&2
printf ' %q' "$@" >&2
printf ' -o /dev/null\n' >&2

$XCC "$@" -o /dev/null
