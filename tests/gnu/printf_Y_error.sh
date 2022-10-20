clean_scratch
mkdir scratch/foo
chmod -x scratch/foo
ln -s foo/bar scratch/bar

bfs_diff scratch -printf '(%p) (%l) %y %Y\n'
ret=$?

chmod +x scratch/foo
clean_scratch

[ $ret -eq $EX_BFS ]
