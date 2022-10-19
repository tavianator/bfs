rm -rf scratch/*
$TOUCH scratch/{foo,bar}

# -links 1 forces a stat() call, which will fail for the second file
invoke_bfs scratch -mindepth 1 -ignore_readdir_race -links 1 -exec "$TESTS/remove-sibling.sh" {} \;
