cd "$TEST"
"$XTOUCH" -p foo/bar

chmod a-r foo
defer chmod +r foo

! bfs_diff . -depth
