! stderr=$(invoke_bfs basic -exec "$TESTS/nonexistent" {} + 2>&1 >/dev/null)
[ -n "$stderr" ]

! bfs_diff basic -exec "$TESTS/nonexistent" {} + -print
