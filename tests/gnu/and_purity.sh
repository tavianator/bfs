# Regression test: (-a lhs(pure) rhs(always_false)) <==> rhs is only valid if rhs is pure
bfs_diff basic -name nonexistent \( -print , -false \)
