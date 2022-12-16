test "$SUDO" || skip
test "$UNAME" = "Darwin" && skip

clean_scratch
mkdir scratch/{foo,mnt}

sudo mount -t tmpfs tmpfs scratch/mnt
trap "sudo umount scratch/mnt" EXIT

"$XTOUCH" scratch/foo/bar scratch/mnt/baz

bfs_diff scratch -mount
