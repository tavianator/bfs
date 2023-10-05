command -v unbuffer &>/dev/null || skip

unset NO_COLOR
unbuffer "${BFS[@]}" rainbow >"$OUT"
sort_output
diff_output
