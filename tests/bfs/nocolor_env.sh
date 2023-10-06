command -v unbuffer &>/dev/null || skip

NO_COLOR=1 bfs_pty rainbow >"$OUT"
sort_output
diff_output
