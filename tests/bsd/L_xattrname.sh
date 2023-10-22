invoke_bfs . -quit -xattr || skip
make_xattrs || skip

case "$UNAME" in
    Darwin|FreeBSD)
        bfs_diff -L . -xattrname bfs_test
        ;;
    *)
        bfs_diff -L . -xattrname security.bfs_test
        ;;
esac
