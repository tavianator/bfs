# -okdir should *not* close stdin
yes | bfs_diff basic -okdir bash -c 'printf "%s? " "$1" && head -n1' bash {} \;
