invoke_bfs basic/a -print0 -exec echo found {} + >"$OUT"
diff_output
