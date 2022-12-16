test -e /dev/full || skip
fail invoke_bfs basic/a -fprint /dev/full -exec true \;
