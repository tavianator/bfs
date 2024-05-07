# POSIX says it's okay to either stop or keep going on seeing a filesystem
# loop, as long as a diagnostic is printed
invoke_bfs -L loops >/dev/null 2>"$OUT" && fail
test -s "$OUT"
