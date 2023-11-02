ulimit -Sn 16
bfs_diff deep -type f -exec bash -c 'printf "%d %s\n" $(ulimit -Sn) "${1:0:6}/.../${1##*/}"' bash {} \;
