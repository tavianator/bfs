# Make sure find's children inherit SIGHUP being ignored under nohup

cd "$TEST"

bfs_verbose . -exec sh -c 'kill -HUP $$; echo ok' \;
nohup "$PTYX" "${BFS[@]}" . -exec sh -c 'kill -HUP $$; echo ok' \; >"$OUT"
sort_output
diff_output || exit $EX_DIFF
