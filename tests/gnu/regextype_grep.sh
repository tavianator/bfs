invoke_bfs -regextype grep -quit || skip

bfs_diff basic -regextype grep -regex '.*/f\+o\?o\?'
