check_exit 42 invoke_bfs basic -name foo -exit 42

check_exit 0 invoke_bfs basic -name qux -exit 42

bfs_diff basic/g -print -name g -exit
