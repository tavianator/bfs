skip_unless test -e /dev/full
fail invoke_bfs basic/a -fprint /dev/full -exec true \;
