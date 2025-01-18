# Regression test: restore the signal mask after fork()

test "$UNAME" = "Linux" || skip
bfs_diff /proc/self/status -exec grep '^SigBlk:' {} +
