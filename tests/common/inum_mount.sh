test "$SUDO" || skip
test "$UNAME" = "Darwin" && skip

clean_scratch
mkdir scratch/{foo,mnt}

sudo mount -t tmpfs tmpfs scratch/mnt
trap "sudo umount scratch/mnt" EXIT

bfs_diff scratch -inum "$(inum scratch/mnt)"
