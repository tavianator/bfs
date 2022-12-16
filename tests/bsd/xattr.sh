invoke_bfs scratch -quit -xattr || skip
make_xattrs || skip
bfs_diff scratch -xattr
