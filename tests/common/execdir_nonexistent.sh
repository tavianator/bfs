! stderr=$(invoke_bfs basic -execdir "$TESTS/nonexistent" {} \; 2>&1 >/dev/null)
[ -n "$stderr" ]

check_exit $EX_BFS bfs_diff basic -print -execdir "$TESTS/nonexistent" {} \; -print
