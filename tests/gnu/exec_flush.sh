# I/O streams should be flushed before executing programs
invoke_bfs basic -print0 -exec echo found \; | tr '\0' ' ' >"$OUT"
sort_output
diff_output
