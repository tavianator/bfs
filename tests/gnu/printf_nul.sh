# NUL byte regression test
invoke_bfs basic/a basic/b -maxdepth 0 -printf '%h\0%f\n' >"$OUT"
diff_output
