cd "$TEST"

"$XTOUCH" -p unreadable/file unreadable/dir/file
chmod a-r unreadable

# Not all platforms have O_SEARCH/O_PATH, but we still shouldn't crash
case "$UNAME" in
    Linux|FreeBSD|SunOS|Darwin)
        ;;
    *)
        invoke_bfs unreadable/{file,dir} -execdir true {} \; || skip
        ;;
esac

bfs_diff unreadable/{file,dir} -execdir ls -d {} \;
