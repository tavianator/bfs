invoke_bfs basic -name foo -exit 42
if [ $? -ne 42 ]; then
    return 1
fi

invoke_bfs basic -name qux -exit 42
if [ $? -ne 0 ]; then
    return 1
fi

bfs_diff basic/g -print -name g -exit
