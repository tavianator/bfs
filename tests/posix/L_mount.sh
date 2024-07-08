test "$UNAME" = "Darwin" && skip

cd "$TEST"
mkdir foo mnt

bfs_sudo mount -t tmpfs tmpfs mnt || skip
defer bfs_sudo umount mnt

ln -s ../mnt foo/bar
"$XTOUCH" mnt/baz
ln -s ../mnt/baz foo/qux

bfs_diff -L . -mount
