test -e /dev/full || skip
! invoke_bfs basic/a -fprint /dev/full -exec true \;
