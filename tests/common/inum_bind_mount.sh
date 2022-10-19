skip_unless test "$SUDO"
skip_unless test "$UNAME" = "Linux"

rm -rf scratch/*
$TOUCH scratch/{foo,bar}
sudo mount --bind scratch/{foo,bar}

bfs_diff scratch -inum "$(inum scratch/bar)"
ret=$?

sudo umount scratch/bar
return $ret
