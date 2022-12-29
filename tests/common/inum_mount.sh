test "$UNAME" = "Darwin" && skip

clean_scratch
mkdir scratch/{foo,mnt}

bfs_sudo mount -t tmpfs tmpfs scratch/mnt || skip
trap "bfs_sudo umount scratch/mnt" EXIT

bfs_diff scratch -inum "$(inum scratch/mnt)"
