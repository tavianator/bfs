fstype=$(invoke_bfs basic -maxdepth 0 -printf '%F\n')
bfs_diff basic -fstype "$fstype"
