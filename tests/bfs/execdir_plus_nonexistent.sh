bfs_diff basic -execdir "$TESTS/nonexistent" {} + -print 2>"$TEST/err" && fail
test -s "$TEST/err"
