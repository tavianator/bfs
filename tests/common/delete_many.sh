# Test for https://github.com/tavianator/bfs/issues/67

cd "$TEST"
mkdir foo
"$XTOUCH" foo/{1..256}

invoke_bfs foo -delete
bfs_diff .
