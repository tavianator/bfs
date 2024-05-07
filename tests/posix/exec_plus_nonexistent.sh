bfs_diff basic -exec "$TESTS/nonexistent" {} + -print 2>"$TEST/err" && fail
test -s "$TEST/err"
