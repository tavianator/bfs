test "$SUDO" || skip
test "$UNAME" = "Linux" || skip

clean_scratch
"$XTOUCH" scratch/{file,null}
sudo mount --bind /dev/null scratch/null

bfs_diff scratch -type c
ret=$?

sudo umount scratch/null
return $ret
