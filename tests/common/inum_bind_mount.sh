test "$UNAME" = "Linux" || skip

clean_scratch
"$XTOUCH" scratch/{foo,bar}

bfs_sudo mount --bind scratch/{foo,bar} || skip
trap "bfs_sudo umount scratch/bar" EXIT

bfs_diff scratch -inum "$(inum scratch/bar)"
