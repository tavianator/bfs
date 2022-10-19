closefrom 4

ulimit -n 16
bfs_diff deep -type f -exec bash -c 'echo "${1:0:6}/.../${1##*/} (${#1})"' bash {} \;
