test -e /dev/full || skip
fail invoke_bfs basic -maxdepth 0 -fprint /dev/full >/dev/full
