stderr=$(invoke_bfs basic -exec "$TESTS/nonexistent" {} + 2>&1 >/dev/null)
[ -n "$stderr" ] || return 1

bfs_diff basic -exec "$TESTS/nonexistent" {} + -print
(($? == EX_BFS))
