# Birth times may not be supported, so just check that %w/%W/%B can be parsed
bfs_diff times -false -printf '%w %WY %BY\n'
