test "$UNAME" = "Darwin" && skip

clean_scratch
mkdir scratch/{foo,mnt}

bfs_sudo mount -t tmpfs tmpfs scratch/mnt || skip
defer bfs_sudo umount scratch/mnt

"$XTOUCH" scratch/foo/bar scratch/mnt/baz

bfs_diff scratch -mount
