stderr=$(invoke_bfs basic -execdir "$TESTS/nonexistent" {} + 2>&1 >/dev/null)
[ -n "$stderr" ] || return 1

bfs_diff basic -execdir "$TESTS/nonexistent" {} + -print
(($? == EX_BFS))
