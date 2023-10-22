test "$UNAME" = "Linux" || skip

cd "$TEST"
"$XTOUCH" file null

bfs_sudo mount --bind /dev/null null || skip
defer bfs_sudo umount null

bfs_diff . -type c
