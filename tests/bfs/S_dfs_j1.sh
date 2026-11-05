# Without -s, only the root order is predictable, so use a single chain of
# directories under each root to fully determine the expected output
cd "$TEST"
mkdir -p a/b/c/d e/f/g/h

invoke_bfs -S dfs -j1 a e >"$OUT"
diff_output
