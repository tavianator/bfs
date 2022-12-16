# Regression test: /dev/full should not fail until actually written to
test -e /dev/full || skip
invoke_bfs basic -false -fprint /dev/full
