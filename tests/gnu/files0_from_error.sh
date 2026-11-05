stderr=$(! invoke_bfs -files0-from basic 2>&1 >/dev/null)
[ -n "$stderr" ]
