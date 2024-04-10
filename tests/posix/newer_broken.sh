ln -s nowhere "$TEST/broken"
"$XTOUCH" -h -t "1991-12-14 00:03" "$TEST/broken"

bfs_diff times -newer "$TEST/broken"
