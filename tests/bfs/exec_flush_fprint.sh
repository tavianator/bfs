# Even non-stdstreams should be flushed
bfs_diff basic/a -fprint "$OUT.f" -exec cat "$OUT.f" \;
