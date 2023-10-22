cd "$TEST"
"$XTOUCH" foo bar

# -links 1 forces a stat() call, which will fail for the second file
invoke_bfs . -mindepth 1 -ignore_readdir_race -links 1 -exec "$TESTS/remove-sibling.sh" {} \;
