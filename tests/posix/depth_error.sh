clean_scratch
"$XTOUCH" -p scratch/foo/bar

chmod a-r scratch/foo
trap "chmod +r scratch/foo" EXIT

! bfs_diff scratch -depth
