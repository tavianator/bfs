ulimit -n 16

# -mindepth 18, but POSIX
path="*/*/*/*/*/*"
path="$path/$path/$path"
bfs_diff deep -path "deep/$path" -nogroup

