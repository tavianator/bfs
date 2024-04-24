cd "$TEST"

"$XTOUCH" -p foo/ bar/ baz qux
chmod -r foo baz
defer chmod +r foo baz

! bfs_diff . -empty
