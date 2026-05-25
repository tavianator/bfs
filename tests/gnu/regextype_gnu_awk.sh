invoke_bfs -regextype gnu-awk -quit || skip

bfs_diff weirdnames -regextype gnu-awk -regex '.*/[\[\*]/(\<.\>)'
