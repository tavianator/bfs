cd weirdnames
invoke_bfs -mindepth 1 -printf "%P\0" | bfs_diff -files0-from - -files0-from -
