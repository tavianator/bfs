invoke_bfs . -quit -xattr || skip
make_xattrs || skip
bfs_diff -L . -xattr
