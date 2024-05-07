invoke_bfs -quit -iwholename PATTERN || skip
bfs_diff basic -iwholename 'basic/*F*'
