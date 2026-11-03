# Regression test: missing always_{true,false} updates led to assertion failures
bfs_diff basic \( -print , -true \) -a -false -o -false
