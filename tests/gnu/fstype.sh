fstype=$(invoke_bfs basic -maxdepth 0 -printf '%F\n') || skip
bfs_diff basic -fstype "$fstype"
