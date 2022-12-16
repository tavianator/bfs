test -e /dev/full || skip
bfs_diff -D all basic 2>/dev/full
