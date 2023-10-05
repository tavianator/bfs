command -v unbuffer &>/dev/null || skip

unbuffer "${BFS[@]}" basic -status >"$OUT"
