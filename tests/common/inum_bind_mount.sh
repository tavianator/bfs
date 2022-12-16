test "$SUDO" || skip
test "$UNAME" = "Linux" || skip

clean_scratch
"$XTOUCH" scratch/{foo,bar}

sudo mount --bind scratch/{foo,bar}
trap "sudo umount scratch/bar" EXIT

bfs_diff scratch -inum "$(inum scratch/bar)"
