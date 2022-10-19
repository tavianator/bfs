TMP_REAL=$(cd "$TMP" && pwd)
OFFSET=$((${#TMP_REAL} + 2))
bfs_diff basic -execdir bash -c "pwd | cut -b$OFFSET-" \;
