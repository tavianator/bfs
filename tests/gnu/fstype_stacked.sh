test "$SUDO" || skip
test "$UNAME" = "Linux" || skip

clean_scratch
mkdir scratch/mnt

sudo mount -t tmpfs tmpfs scratch/mnt
trap "sudo umount scratch/mnt" EXIT

sudo mount -t ramfs ramfs scratch/mnt
trap "sudo umount scratch/mnt; sudo umount scratch/mnt" EXIT

bfs_diff scratch/mnt -fstype ramfs -print -o -printf '%p: %F\n'
