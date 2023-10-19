clean_scratch
"$XTOUCH" -p scratch/foo/bar

chmod a-r scratch/foo
defer chmod +r scratch/foo

! bfs_diff scratch -depth
