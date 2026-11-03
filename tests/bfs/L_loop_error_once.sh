stderr=$(! invoke_bfs -L loops 2>&1 >/dev/null)
[ "$(grep -c 'loops/loop:' <<<"$stderr")" -eq 1 ]
