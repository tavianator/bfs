clean_scratch
"$XTOUCH" -p scratch/foo/bar/baz

# Don't try to delete '.'
(cd scratch && invoke_bfs . -delete)

bfs_diff scratch
