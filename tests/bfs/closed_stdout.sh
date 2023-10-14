# Check if the platform automatically re-opens stdout before we can
(bash -c echo >&-) && skip

! invoke_bfs basic >&-
