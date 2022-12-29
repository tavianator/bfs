test "$UNAME" = "Linux" || skip

clean_scratch
"$XTOUCH" scratch/{file,null}

bfs_sudo mount --bind /dev/null scratch/null || skip
trap "bfs_sudo umount scratch/null" EXIT

bfs_diff scratch -type c
