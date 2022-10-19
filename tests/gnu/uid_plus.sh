skip_if test "$(id -u)" -eq 0
bfs_diff basic -uid +0
