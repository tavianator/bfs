rm -rf scratch/*
touchp scratch/foo/bar/baz

(cd scratch && invoke_bfs . -rm)

bfs_diff scratch
