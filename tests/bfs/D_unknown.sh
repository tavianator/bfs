stderr=$(invoke_bfs -warn -D unknown basic 2>&1 >"$OUT")
[ -n "$stderr" ] || return 1
sort_output
diff_output
