invoke_bfs -regextype findutils-default -quit || skip

bfs_diff weirdnames -regextype findutils-default -regex '.*/./\(m\|n\)'
