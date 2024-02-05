tree=$(invoke_bfs -D tree 2>&1 -quit)
[[ "$tree" == *"-S dfs"* ]] && skip

bfs_diff -j1 basic -execdir "$TESTS/sort-args.sh" {} +
