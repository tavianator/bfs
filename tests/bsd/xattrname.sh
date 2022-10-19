skip_unless invoke_bfs scratch -quit -xattr
skip_unless make_xattrs

case "$UNAME" in
    Darwin|FreeBSD)
        bfs_diff scratch -xattrname bfs_test
        ;;
    *)
        bfs_diff scratch -xattrname security.bfs_test
        ;;
esac
