test "$UNAME" = "Linux" || skip

clean_scratch
"$XTOUCH" scratch/{file,null}
ln -s /dev/null scratch/link

bfs_sudo mount --bind /dev/null scratch/null || skip
defer bfs_sudo umount scratch/null

bfs_diff -L scratch -type c
