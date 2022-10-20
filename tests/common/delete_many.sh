# Test for https://github.com/tavianator/bfs/issues/67

clean_scratch
mkdir scratch/foo
$TOUCH scratch/foo/{1..256}

invoke_bfs scratch/foo -delete
bfs_diff scratch
