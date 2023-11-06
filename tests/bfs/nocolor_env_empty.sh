command -v unbuffer &>/dev/null || skip

NO_COLOR= bfs_pty rainbow >"$OUT"
sort_output
diff_output
