printf "basic\nbasic\n" >"$OUT"

invoke_bfs basic -maxdepth 0 -fprint "$OUT"
sort_output
diff_output
