stderr=$(invoke_bfs -warn -exclude basic -name '*f*' 2>&1 >/dev/null)
[ -n "$stderr" ]
