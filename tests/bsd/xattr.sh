skip_unless invoke_bfs scratch -quit -xattr
skip_unless make_xattrs
bfs_diff scratch -xattr
