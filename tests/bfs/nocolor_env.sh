command -v unbuffer &>/dev/null || skip

NO_COLOR=1 unbuffer "${BFS[@]}" rainbow >"$OUT"
sort_output
diff_output
