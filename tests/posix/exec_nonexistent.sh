# Failure to execute the command should lead to an error message and
# non-zero exit status.  See https://unix.stackexchange.com/q/704522/56202
bfs_diff basic -print -exec "$TESTS/nonexistent" {} \; -print 2>"$TEST/err" && fail
test -s "$TEST/err"
