test -e /dev/full || skip
! invoke_bfs basic/a -print0 -exec echo found {} + >/dev/full
