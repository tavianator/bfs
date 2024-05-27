# Make sure -ignore_readdir_race doesn't suppress ELOOP from an actual filesystem loop
! bfs_diff -L loops -ignore_readdir_race
