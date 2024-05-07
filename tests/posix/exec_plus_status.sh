# -exec ... {} + should always return true, but if the command fails, bfs
# should exit with a non-zero status
! bfs_diff basic -exec false {} + -print
