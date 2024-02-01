ulimit -n $((NOPENFD + 13))
[ "$(invoke_bfs deep -printf '%u %g\n' | uniq)" = "$(id -un) $(id -gn)" ]
