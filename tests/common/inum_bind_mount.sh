test "$UNAME" = "Linux" || skip

cd "$TEST"
"$XTOUCH" foo bar baz

bfs_sudo mount --bind foo bar || skip
defer bfs_sudo umount bar

bfs_diff . -inum "$(inum bar)"
