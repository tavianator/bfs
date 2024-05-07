stderr=$(invoke_bfs -warn -O9 basic 2>&1 >"$OUT")
[ -n "$stderr" ]
sort_output
diff_output
