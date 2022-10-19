skip_unless invoke_bfs scratch -quit -xattr
skip_unless make_xattrs

case "$UNAME" in
    Darwin|FreeBSD)
        bfs_diff -L scratch -xattrname bfs_test
        ;;
    *)
        bfs_diff -L scratch -xattrname security.bfs_test
        ;;
esac
