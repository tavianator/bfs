skip_unless test "$SUDO"
skip_if test "$UNAME" = "Darwin"

clean_scratch
mkdir scratch/{foo,mnt}
sudo mount -t tmpfs tmpfs scratch/mnt

bfs_diff scratch -inum "$(inum scratch/mnt)"
ret=$?

sudo umount scratch/mnt
return $ret
