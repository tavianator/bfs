"$XTOUCH" -p "$TEST/foo.out"
ln "$TEST/foo.out" "$TEST/foo.hard"
ln -s foo.out "$TEST/foo.soft"

invoke_bfs basic -fprint "$TEST/foo.out" -fprint "$TEST/foo.hard" -fprint "$TEST/foo.soft"
sort "$TEST/foo.out" >"$OUT"
diff_output
