tree=$(invoke_bfs -D tree 2>&1 -quit)
[[ "$tree" == *"-S dfs"* ]] && skip

bfs_diff basic -execdir "$TESTS/sort-args.sh" {} +
