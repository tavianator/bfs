! stderr=$(invoke_bfs basic -execdir "$TESTS/nonexistent" {} + 2>&1 >/dev/null)
[ -n "$stderr" ]

! bfs_diff basic -execdir "$TESTS/nonexistent" {} + -print
