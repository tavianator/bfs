# Check if the platform automatically re-opens stderr before we can
(bash -c 'echo >&2' 2>&-) && skip

! invoke_bfs basic >&- 2>&-
