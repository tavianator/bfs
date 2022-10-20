clean_scratch
mkdir scratch/foo
mkdir scratch/bar
ln -s ../foo scratch/bar/baz

# Don't try to rmdir() a symlink
invoke_bfs -L scratch/bar -delete || return 1

bfs_diff scratch
