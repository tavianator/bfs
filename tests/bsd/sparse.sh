test "$UNAME" = "Linux" || skip

cd "$TEST"
mkdir mnt

bfs_sudo mount -t tmpfs tmpfs mnt || skip
defer bfs_sudo umount mnt

truncate -s 1M mnt/sparse
dd if=/dev/zero of=mnt/dense bs=1M count=1

bfs_diff mnt -type f -sparse
