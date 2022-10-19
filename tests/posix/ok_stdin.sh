# -ok should *not* close stdin
# See https://savannah.gnu.org/bugs/?24561
yes | bfs_diff basic -ok bash -c 'printf "%s? " "$1" && head -n1' bash {} \;
