test "$UNAME" = "Darwin" && skip

clean_scratch
mkdir scratch/{foo,mnt}

bfs_sudo mount -t tmpfs tmpfs scratch/mnt || skip
trap "bfs_sudo umount scratch/mnt" EXIT

ln -s ../mnt scratch/foo/bar
"$XTOUCH" scratch/mnt/baz
ln -s ../mnt/baz scratch/foo/qux

bfs_diff -L scratch -mount
