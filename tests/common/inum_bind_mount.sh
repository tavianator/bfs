test "$SUDO" || skip
test "$UNAME" = "Linux" || skip

clean_scratch
"$XTOUCH" scratch/{foo,bar}
sudo mount --bind scratch/{foo,bar}

bfs_diff scratch -inum "$(inum scratch/bar)"
ret=$?

sudo umount scratch/bar
return $ret
