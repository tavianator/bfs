cd "$TEST"

"$XTOUCH" -p foo/bar baz/qux
chmod -w foo
defer chmod +w foo

! invoke_bfs . -print -delete -print >"$OUT" || fail
sort_output
diff_output
