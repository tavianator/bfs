# Regression test: an ignored (-type d -o -type l) shouldn't constrain later expressions
bfs_diff links \( -type d -o -type l \) , \! -type d
