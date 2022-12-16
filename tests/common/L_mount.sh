test "$SUDO" || skip
test "$UNAME" = "Darwin" && skip

clean_scratch
mkdir scratch/{foo,mnt}

sudo mount -t tmpfs tmpfs scratch/mnt
trap "sudo umount scratch/mnt" EXIT

ln -s ../mnt scratch/foo/bar
"$XTOUCH" scratch/mnt/baz
ln -s ../mnt/baz scratch/foo/qux

bfs_diff -L scratch -mount
