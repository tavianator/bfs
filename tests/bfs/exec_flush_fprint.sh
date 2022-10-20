# Even non-stdstreams should be flushed
clean_scratch
bfs_diff basic/a -fprint scratch/foo -exec cat scratch/foo \;
