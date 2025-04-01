FILE="$TMP/$TEST.in"
cd weirdnames
invoke_bfs -mindepth 1 -fprintf "$FILE" "%P\0"
yes | bfs_diff -files0-from - -ok printf '%s\n' {} \; -files0-from "$FILE"
