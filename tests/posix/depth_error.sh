clean_scratch
"$XTOUCH" -p scratch/foo/bar

chmod a-r scratch/foo
trap "chmod +r scratch/foo" EXIT

check_exit $EX_BFS bfs_diff scratch -depth
