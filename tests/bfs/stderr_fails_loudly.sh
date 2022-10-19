skip_unless test -e /dev/full
fail invoke_bfs -D all basic -false -fprint /dev/full 2>/dev/full
