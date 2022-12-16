test -e /dev/full || skip
! invoke_bfs -D all basic -false -fprint /dev/full 2>/dev/full
