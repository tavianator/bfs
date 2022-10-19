# POSIX says it's okay to either stop or keep going on seeing a filesystem
# loop, as long as a diagnostic is printed
errors=$(invoke_bfs -L loops 2>&1 >/dev/null)
[ -n "$errors" ]
