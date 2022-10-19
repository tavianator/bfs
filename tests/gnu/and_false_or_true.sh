# Test (-a lhs(always_true) -false) <==> (! lhs),
# (-o lhs(always_false) -true) <==> (! lhs)
bfs_diff basic -prune -false -o -true
