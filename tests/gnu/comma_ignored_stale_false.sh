# Regression test: missing always_{true,false} updates led to assertion failures
bfs_diff basic \( \( -prune , \( -print -a -false \) \) -o -name x \) -a -name y
