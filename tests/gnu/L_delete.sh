cd "$TEST"
mkdir foo bar
ln -s ../foo bar/baz

# Don't try to rmdir() a symlink
invoke_bfs -L bar -delete

bfs_diff .
