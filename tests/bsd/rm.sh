cd "$TEST"
"$XTOUCH" -p foo/bar/baz
invoke_bfs . -rm
bfs_diff .
