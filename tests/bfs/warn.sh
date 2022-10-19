stderr=$(invoke_bfs basic -warn -depth -prune 2>&1 >/dev/null)
[ -n "$stderr" ]
