#!/bin/bash

for file in "${1%/*}"/*; do
    if [ "$file" != "$1" ]; then
        rm "$file"
        exit $?
    fi
done

exit 1
