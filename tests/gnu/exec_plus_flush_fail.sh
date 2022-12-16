test -e /dev/full || skip
fail invoke_bfs basic/a -print0 -exec echo found {} + >/dev/full
