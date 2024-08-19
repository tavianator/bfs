# bfs shouldn't print "warning: Suppressed errors" without -noerror
! invoke_bfs inaccessible -warn 2>&1 >/dev/null | grep warning >&2
