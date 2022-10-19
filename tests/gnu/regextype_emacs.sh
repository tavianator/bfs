skip_unless invoke_bfs -regextype emacs -quit

bfs_diff basic -regextype emacs -regex '.*/\(f+o?o?\|bar\)'
