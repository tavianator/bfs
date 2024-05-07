test "$UNAME" = "Darwin" && skip

cd "$TEST"
mkdir foo mnt

bfs_sudo mount -t tmpfs tmpfs mnt || skip
defer bfs_sudo umount mnt

"$XTOUCH" foo/bar mnt/baz

bfs_diff . -mount
