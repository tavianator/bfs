test -e /dev/full || skip
fail invoke_bfs basic -maxdepth 0 >/dev/full
