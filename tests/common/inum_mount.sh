test "$SUDO" || skip
test "$UNAME" = "Darwin" && skip

clean_scratch
mkdir scratch/{foo,mnt}
sudo mount -t tmpfs tmpfs scratch/mnt

bfs_diff scratch -inum "$(inum scratch/mnt)"
ret=$?

sudo umount scratch/mnt
return $ret
