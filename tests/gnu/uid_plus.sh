test "$(id -u)" -eq 0 && skip
bfs_diff basic -uid +0
