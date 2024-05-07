LS_COLORS="ec=\33[\0m:*.gz=\0\61;31:" invoke_bfs rainbow -color | tr '\0' '0' >"$OUT"
sort_output
diff_output
