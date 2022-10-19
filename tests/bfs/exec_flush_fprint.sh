# Even non-stdstreams should be flushed
rm -rf scratch/*
bfs_diff basic/a -fprint scratch/foo -exec cat scratch/foo \;
