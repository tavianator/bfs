# Don't prepend ./ for absolute paths in -execdir
bfs_diff / -maxdepth 0 -execdir echo {} \;
