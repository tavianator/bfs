#!/usr/bin/env bash

args=($({ for arg; do echo "$arg"; done } | sort))
echo "${args[@]}"
