tree=$(invoke_bfs -D tree 2>&1 -quit)

if [[ "$tree" == *"-S dfs"* ]]; then
    skip
fi

bfs_diff basic -execdir "$TESTS/sort-args.sh" {} +
