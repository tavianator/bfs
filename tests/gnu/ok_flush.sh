# I/O streams should be flushed before -ok prompts
yes | invoke_bfs basic -printf '%p ? ' -ok echo found \; 2>&1 | sed 's/?.*?/?/' >"$OUT"
sort_output
diff_output
