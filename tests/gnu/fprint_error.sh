test -e /dev/full || skip
! invoke_bfs basic -maxdepth 0 -fprint /dev/full
