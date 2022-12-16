clean_scratch
mkdir scratch/foo
ln -s foo/bar scratch/bar

chmod -x scratch/foo
trap "chmod +x scratch/foo" EXIT

check_exit $EX_BFS bfs_diff scratch -printf '(%p) (%l) %y %Y\n'
