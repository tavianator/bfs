cd weirdnames
invoke_bfs -mindepth 1 -fprintf ../scratch/files0.in "%P\0"
bfs_diff -files0-from ../scratch/files0.in
