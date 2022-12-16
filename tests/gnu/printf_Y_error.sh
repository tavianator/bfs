clean_scratch
mkdir scratch/foo
ln -s foo/bar scratch/bar

chmod -x scratch/foo
trap "chmod +x scratch/foo" EXIT

! bfs_diff scratch -printf '(%p) (%l) %y %Y\n'
