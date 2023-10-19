test "$UNAME" = "Linux" || skip

clean_scratch
mkdir scratch/mnt

bfs_sudo mount -t tmpfs tmpfs scratch/mnt || skip
defer bfs_sudo umount scratch/mnt

bfs_sudo mount -t ramfs ramfs scratch/mnt || skip
defer bfs_sudo umount scratch/mnt

bfs_diff scratch/mnt -fstype ramfs -print -o -printf '%p: %F\n'
