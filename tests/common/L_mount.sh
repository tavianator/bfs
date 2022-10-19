skip_unless test "$SUDO"
skip_if test "$UNAME" = "Darwin"

rm -rf scratch/*
mkdir scratch/{foo,mnt}
sudo mount -t tmpfs tmpfs scratch/mnt
ln -s ../mnt scratch/foo/bar
$TOUCH scratch/mnt/baz
ln -s ../mnt/baz scratch/foo/qux

bfs_diff -L scratch -mount
ret=$?

sudo umount scratch/mnt
return $ret
