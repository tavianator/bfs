invoke_bfs -regextype emacs -quit || skip

bfs_diff basic -regextype emacs -regex '.*/\(f+o?o?\|bar\)'
