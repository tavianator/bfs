invoke_bfs -quit -iname PATTERN || skip
bfs_diff basic -iname '*F*'
