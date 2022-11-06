fstype=$(invoke_bfs basic -maxdepth 0 -printf '%F\n')
skip_if test $? -ne 0
bfs_diff basic -fstype "$fstype"
