! invoke_bfs -help | grep -E '\{...?\}' || fail
! invoke_bfs -D help | grep -E '\{...?\}' || fail
! invoke_bfs -S help | grep -E '\{...?\}' || fail
! invoke_bfs -regextype help | grep -E '\{...?\}' || fail
