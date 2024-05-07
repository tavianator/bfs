cd "$TEST"
"$XTOUCH" -p foo/bar/baz
invoke_bfs . -delete
bfs_diff .
