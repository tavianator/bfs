# Regression test: /dev/full should not fail until actually written to
skip_unless test -e /dev/full
invoke_bfs basic -false -fprint /dev/full
