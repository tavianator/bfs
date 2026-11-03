test "$UNAME" = "Linux" || skip

cd "$TEST"
"$XTOUCH" -p original/foo bound/

fstype=$(invoke_bfs original/foo -printf '%F\n') || skip

bfs_sudo mount --bind original bound || skip
defer bfs_sudo umount bound

bfs_diff original bound -fstype "$fstype"
