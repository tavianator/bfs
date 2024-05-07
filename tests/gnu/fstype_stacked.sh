test "$UNAME" = "Linux" || skip

cd "$TEST"
mkdir mnt

bfs_sudo mount -t tmpfs tmpfs mnt || skip
defer bfs_sudo umount mnt

bfs_sudo mount -t ramfs ramfs mnt || skip
defer bfs_sudo umount mnt

bfs_diff mnt -fstype ramfs -print -o -printf '%p: %F\n'
