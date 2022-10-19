skip_unless test -e /dev/full
bfs_diff -D all basic 2>/dev/full
