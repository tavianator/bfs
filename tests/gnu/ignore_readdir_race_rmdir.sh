cd "$TEST"
"$XTOUCH" -p foo/ bar/

# Check that -ignore_readdir_race suppresses errors from opendir()
bfs_diff . -ignore_readdir_race -mindepth 1 -print -name foo -exec rmdir {} \;
