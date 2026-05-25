# Make sure -xtype is not reordered in front of anything -- if -xtype runs
# before -links 100, it will report an ELOOP error
bfs_diff inaccessible/link -links 100 -xtype l
