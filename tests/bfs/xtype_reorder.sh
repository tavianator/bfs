# Make sure -xtype is not reordered in front of anything -- if -xtype runs
# before -links 100, it will report an ELOOP error
bfs_diff loops -links 100 -xtype l
invoke_bfs loops -links 100 -xtype l
