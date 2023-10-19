clean_scratch
mkdir scratch/foo
ln -s foo/bar scratch/bar

chmod -x scratch/foo
defer chmod +x scratch/foo

! bfs_diff scratch -printf '(%p) (%l) %y %Y\n'
