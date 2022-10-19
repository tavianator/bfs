skip_unless test -e /dev/full
fail invoke_bfs basic/a -print0 -exec echo found {} + >/dev/full
