test "$UNAME" = "Darwin" && skip

clean_scratch
mkdir scratch/{foo,mnt}

bfs_sudo mount -t tmpfs tmpfs scratch/mnt || skip
trap "bfs_sudo umount scratch/mnt" EXIT

"$XTOUCH" scratch/foo/bar scratch/mnt/baz

bfs_diff scratch -xdev
