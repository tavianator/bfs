ulimit -Sn 16
bfs_diff deep -type f -execdir bash -c 'printf "%d %s\n" $(ulimit -Sn) "$1"' bash {} \;
