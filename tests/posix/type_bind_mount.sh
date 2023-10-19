test "$UNAME" = "Linux" || skip

clean_scratch
"$XTOUCH" scratch/{file,null}

bfs_sudo mount --bind /dev/null scratch/null || skip
defer bfs_sudo umount scratch/null

bfs_diff scratch -type c
