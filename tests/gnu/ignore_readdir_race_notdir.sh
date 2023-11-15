# Check -ignore_readdir_race handling when a directory is replaced with a file
cd "$TEST"
mkdir foo

invoke_bfs . -mindepth 1 -ignore_readdir_race \
    -type d -execdir rmdir {} \; \
    -execdir "$XTOUCH" {} \;
