stderr=$(invoke_bfs inaccessible -noerror -warn 2>&1 >/dev/null)
[ -n "$stderr" ]
