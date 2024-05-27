invoke_bfs -regextype posix-awk -quit || skip

bfs_diff weirdnames -regextype posix-awk -regex '.*/[\[\*]/.*'
