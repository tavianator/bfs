rm -rf scratch/*
touchp scratch/foo/bar
chmod a-r scratch/foo

bfs_diff scratch -depth
ret=$?

chmod +r scratch/foo
rm -rf scratch/*

[ $ret -eq $EX_BFS ]
