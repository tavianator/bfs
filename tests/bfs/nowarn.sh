stderr=$(invoke_bfs basic -nowarn -depth -prune 2>&1 >/dev/null)
[ -z "$stderr" ]
