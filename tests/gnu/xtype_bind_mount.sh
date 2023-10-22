test "$UNAME" = "Linux" || skip

cd "$TEST"
"$XTOUCH" file null
ln -s /dev/null link

bfs_sudo mount --bind /dev/null null || skip
defer bfs_sudo umount null

bfs_diff . -xtype c
