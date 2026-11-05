cd "$TEST"
mkdir -p a/b/c/d e/f/g/h

invoke_bfs -S dfs -j1 a e -links +0 >"$OUT"
diff_output
