(
    unset PATH
    invoke_bfs basic -exec echo {} \; >"$OUT"
)

sort_output
diff_output
