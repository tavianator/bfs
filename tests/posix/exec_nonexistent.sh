# Failure to execute the command should lead to an error message and
# non-zero exit status.  See https://unix.stackexchange.com/q/704522/56202

stderr=$(invoke_bfs basic -exec "$TESTS/nonexistent" {} \; 2>&1 >/dev/null)
[ -n "$stderr" ] || return 1

bfs_diff basic -print -exec "$TESTS/nonexistent" {} \; -print
(($? == EX_BFS))
