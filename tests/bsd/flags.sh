invoke_bfs . -quit -flags offline || skip

cd "$TEST"

"$XTOUCH" foo bar
chflags offline bar || skip

bfs_diff . -flags -offline,nohidden
