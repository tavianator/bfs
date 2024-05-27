cd "$TEST"
"$XTOUCH" -p foo/bar
ln -s foo/bar baz

chmod a-rx foo
defer chmod +rx foo

! bfs_diff -L . -name '???' -prune -o -print
