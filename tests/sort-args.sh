#!/usr/bin/env bash

IFS=$'\n' read -rd '' -a args < <(printf '%s\n' "$@" | sort)
echo "${args[@]}"
