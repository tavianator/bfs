invoke_bfs -regextype awk -quit || skip

bfs_diff weirdnames -regextype awk -regex '.*/[\[\*]/.*'
