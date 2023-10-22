# Check -ignore_readdir_race handling when a directory is replaced with a file
cd "$TEST"
"$XTOUCH" -p foo/bar

invoke_bfs . -mindepth 1 -ignore_readdir_race -execdir rm -r {} \; -execdir "$XTOUCH" {} \;
