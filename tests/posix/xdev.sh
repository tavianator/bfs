skip_unless test "$SUDO"
skip_if test "$UNAME" = "Darwin"

rm -rf scratch/*
mkdir scratch/{foo,mnt}
sudo mount -t tmpfs tmpfs scratch/mnt
$TOUCH scratch/foo/bar scratch/mnt/baz

bfs_diff scratch -xdev
ret=$?

sudo umount scratch/mnt
return $ret
