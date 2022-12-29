test "$UNAME" = "Linux" || skip

clean_scratch
"$XTOUCH" scratch/{file,null}
ln -s /dev/null scratch/link

bfs_sudo mount --bind /dev/null scratch/null || skip
trap "bfs_sudo umount scratch/null" EXIT

bfs_diff -L scratch -type c
