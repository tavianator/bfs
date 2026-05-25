stderr=$(invoke_bfs inaccessible -noerror -nowarn 2>&1 >/dev/null)
[ -z "$stderr" ]
