skip_unless test -e /dev/full
fail invoke_bfs basic -maxdepth 0 >/dev/full
