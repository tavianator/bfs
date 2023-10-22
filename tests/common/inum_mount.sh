test "$UNAME" = "Darwin" && skip

cd "$TEST"
mkdir foo mnt

bfs_sudo mount -t tmpfs tmpfs mnt || skip
defer bfs_sudo umount mnt

bfs_diff . -inum "$(inum mnt)"
