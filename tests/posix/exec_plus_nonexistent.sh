! stderr=$(invoke_bfs basic -exec "$TESTS/nonexistent" {} + 2>&1 >/dev/null)
[ -n "$stderr" ]

check_exit $EX_BFS bfs_diff basic -exec "$TESTS/nonexistent" {} + -print
