# Regression test: bfs -S ids -s -name foo -quit would not actually quit,
# ending up in a confused state and erroring/crashing

bfs_diff -s basic -name foo -print -quit
