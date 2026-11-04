# Make sure find's children inherit SIGUSR1 being ignored

trap '' USR1
bfs_diff basic -prune -exec sh -c 'kill -USR1 $$' \; -print
