[[ "$UNAME" == *BSD* ]] || skip

cd "$TEST"
"$XTOUCH" -p lower/{foo,bar,baz} upper/{bar,baz/qux}

bfs_sudo mount -t unionfs -o below lower upper || skip
defer bfs_sudo umount upper

bfs_diff .
